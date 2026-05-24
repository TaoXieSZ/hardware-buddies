"""cmux control: enumerate sessions and route verbatim text into a chosen one.

Backs the voice control-plane secretary. All cmux interaction goes through an
injectable `runner` so the pure logic (parsing `workspace.list` JSON,
number→UUID resolution, argv building) is host-testable and the subprocess is
mockable.

Targeting binds to the stable workspace **UUID** (`id`), never the positional
`workspace:N` ref (refs shift as workspaces open/close).

Source of truth for the session list is `cmux rpc workspace.list '{}'` (JSON),
not the human `list-workspaces` text — the JSON carries id, title, index,
current_directory, selected.
"""
from __future__ import annotations

import json
import subprocess
from dataclasses import dataclass
from typing import Callable, Optional, Sequence

DEFAULT_CMUX = "/Applications/cmux.app/Contents/Resources/bin/cmux"

# A Runner runs an argv and returns (returncode, stdout, stderr).
Runner = Callable[[Sequence[str]], "tuple[int, str, str]"]


@dataclass(frozen=True)
class Session:
    number: int        # 1-based, stable position for the board / voice "say 2"
    ref: str           # workspace:N (positional, volatile — do not target by this)
    uuid: str          # stable id — ALL targeting uses this
    title: str
    cwd: str
    selected: bool


def parse_workspaces(stdout: str) -> list[Session]:
    """Parse `cmux rpc workspace.list` JSON into ordered Sessions (pure).

    Number is 1-based by the workspace's own `index` (falls back to list order).
    """
    data = json.loads(stdout)
    raw = data.get("workspaces", [])
    sessions: list[Session] = []
    for i, w in enumerate(raw):
        idx = w.get("index")
        number = (idx + 1) if isinstance(idx, int) else (i + 1)
        sessions.append(
            Session(
                number=number,
                ref=w.get("ref", ""),
                uuid=w.get("id", ""),
                title=w.get("title") or w.get("name") or "",
                cwd=w.get("current_directory") or "",
                selected=bool(w.get("selected")),
            )
        )
    sessions.sort(key=lambda s: s.number)
    return sessions


def resolve(number: int, sessions: Sequence[Session]) -> Optional[str]:
    """Return the UUID for the given 1-based number, or None if unknown."""
    for s in sessions:
        if s.number == number:
            return s.uuid
    return None


def _default_runner(argv: Sequence[str]) -> "tuple[int, str, str]":
    p = subprocess.run(list(argv), capture_output=True, text=True)
    return p.returncode, p.stdout, p.stderr


class CmuxClient:
    """Thin wrapper over the cmux CLI with an injectable runner."""

    def __init__(self, binary: str = DEFAULT_CMUX, runner: Optional[Runner] = None):
        self.binary = binary
        self.run = runner or _default_runner

    # --- argv builders (pure, asserted in tests) ---------------------------
    def _send_argv(self, uuid: str, text: str) -> list[str]:
        return [self.binary, "send", "--workspace", uuid, text]

    def _send_key_argv(self, uuid: str, key: str) -> list[str]:
        return [self.binary, "send-key", "--workspace", uuid, key]

    def _read_argv(self, uuid: str, lines: int) -> list[str]:
        return [self.binary, "read-screen", "--workspace", uuid, "--lines", str(lines)]

    # --- operations --------------------------------------------------------
    def list_sessions(self) -> list[Session]:
        rc, out, err = self.run([self.binary, "rpc", "workspace.list", "{}"])
        if rc != 0:
            raise RuntimeError(f"cmux workspace.list failed: {err.strip() or rc}")
        return parse_workspaces(out)

    def send_text(self, uuid: str, text: str) -> "tuple[int, str, str]":
        return self.run(self._send_argv(uuid, text))

    def send_enter(self, uuid: str) -> "tuple[int, str, str]":
        return self.run(self._send_key_argv(uuid, "Enter"))

    def route(self, number: int, text: str) -> str:
        """Type `text` verbatim into session `number` and submit (Enter).

        Sends text then a separate Enter key (unambiguous vs. embedding \\n).
        Returns the target UUID. Raises if the number is unknown.
        """
        sessions = self.list_sessions()
        uuid = resolve(number, sessions)
        if not uuid:
            raise KeyError(f"no session numbered {number}")
        self.send_text(uuid, text)
        self.send_enter(uuid)
        return uuid

    def read_status(self, number: int, lines: int = 6) -> str:
        """Return the last non-empty line of session `number`'s screen."""
        sessions = self.list_sessions()
        uuid = resolve(number, sessions)
        if not uuid:
            raise KeyError(f"no session numbered {number}")
        rc, out, err = self.run(self._read_argv(uuid, lines))
        if rc != 0:
            return ""
        nonempty = [ln for ln in out.splitlines() if ln.strip()]
        return nonempty[-1].strip() if nonempty else ""
