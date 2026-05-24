"""cmux control: enumerate terminal *surfaces* (panes) and route verbatim text
into a chosen one.

Backs the voice control-plane secretary. A "session" here is a cmux **surface**
(a terminal pane), NOT a workspace — coding agents commonly run as splits inside
one workspace, so surface-level addressing is what matches real usage. (cmux's
fundamental unit is the surface; a workspace is just a tab holding panes.)

All cmux interaction goes through an injectable `runner`, so the pure logic
(filtering, numbering, argv building) is host-testable and the subprocess is
mockable.

Targeting binds to the stable **surface UUID** (`id`), never the positional
`surface:N` ref (refs shift as panes open/close).

The board's own pane (running `control_plane.board`) and browser surfaces (the
voice agent) are excluded by a deterministic filter, so the board and the daemon
compute the SAME numbering independently.

Session metadata note: cmux exposes `current_directory` per workspace but NOT
per surface, so `cwd` is the owning workspace's directory; the surface `title`
(repo·id for a Claude Code pane, user@host:~/path for a shell) disambiguates.
"""
from __future__ import annotations

import json
import subprocess
from dataclasses import dataclass
from typing import Callable, Optional, Sequence

DEFAULT_CMUX = "/Applications/cmux.app/Contents/Resources/bin/cmux"

# A surface whose title contains this is the board itself — never a target.
BOARD_MARKER = "control_plane.board"

# A Runner runs an argv and returns (returncode, stdout, stderr).
Runner = Callable[[Sequence[str]], "tuple[int, str, str]"]


@dataclass(frozen=True)
class Session:
    number: int        # 1-based, stable; what the board shows / the voice says
    surface: str       # stable surface UUID — ALL targeting uses this
    workspace: str     # owning workspace UUID
    title: str         # terminal title (repo·id for Claude Code, user@host:~/p for a shell)
    cwd: str           # owning workspace current_directory (cmux has no per-pane cwd)
    focused: bool      # the currently focused pane

    # Back-compat alias for board.build_board(), which reads `.selected`.
    @property
    def selected(self) -> bool:
        return self.focused


def build_sessions(workspaces_json: str, surfaces_by_ws: dict[str, str]) -> list[Session]:
    """Build the ordered session list (pure) from workspace + surface JSON.

    `surfaces_by_ws` maps a workspace UUID to that workspace's
    `cmux rpc surface.list` JSON. Terminal surfaces are numbered 1-based across
    all workspaces in (workspace index, surface index) order; browser surfaces
    and the board's own pane are skipped so numbering is stable and shared.
    """
    workspaces = json.loads(workspaces_json).get("workspaces", [])
    workspaces = sorted(workspaces, key=lambda w: w.get("index", 0))
    sessions: list[Session] = []
    n = 0
    for w in workspaces:
        ws_id = w.get("id", "")
        cwd = w.get("current_directory") or ""
        ws_selected = bool(w.get("selected"))  # only the active tab carries the focus
        raw = surfaces_by_ws.get(ws_id)
        if not raw:
            continue
        surfaces = json.loads(raw).get("surfaces", [])
        surfaces = sorted(surfaces, key=lambda s: s.get("index", 0))
        for s in surfaces:
            if s.get("type") != "terminal":
                continue
            title = s.get("title") or ""
            if BOARD_MARKER in title:
                continue
            n += 1
            sessions.append(
                Session(
                    number=n,
                    surface=s.get("id", ""),
                    workspace=ws_id,
                    title=title,
                    cwd=cwd,
                    # `focused` is per-workspace; the globally active pane is the
                    # focused surface inside the *selected* workspace.
                    focused=bool(s.get("focused")) and ws_selected,
                )
            )
    return sessions


def resolve(number: int, sessions: Sequence[Session]) -> Optional[str]:
    """Return the surface UUID for the given 1-based number, or None."""
    for s in sessions:
        if s.number == number:
            return s.surface
    return None


def _last_nonempty(text: str) -> str:
    nonempty = [ln for ln in text.splitlines() if ln.strip()]
    return nonempty[-1].strip() if nonempty else ""


def _default_runner(argv: Sequence[str]) -> "tuple[int, str, str]":
    p = subprocess.run(list(argv), capture_output=True, text=True)
    return p.returncode, p.stdout, p.stderr


class CmuxClient:
    """Thin wrapper over the cmux CLI with an injectable runner."""

    def __init__(self, binary: str = DEFAULT_CMUX, runner: Optional[Runner] = None):
        self.binary = binary
        self.run = runner or _default_runner

    # --- argv builders (pure, asserted in tests) ---------------------------
    def _rpc_argv(self, method: str, params: dict) -> list[str]:
        return [self.binary, "rpc", method, json.dumps(params)]

    def _send_text_argv(self, surface: str, text: str) -> list[str]:
        return self._rpc_argv("surface.send_text", {"surface_id": surface, "text": text})

    def _send_key_argv(self, surface: str, key: str) -> list[str]:
        return self._rpc_argv("surface.send_key", {"surface_id": surface, "key": key})

    def _focus_argv(self, surface: str) -> list[str]:
        return self._rpc_argv("surface.focus", {"surface_id": surface})

    def _read_argv(self, surface: str) -> list[str]:
        return self._rpc_argv("surface.read_text", {"surface_id": surface})

    # --- operations --------------------------------------------------------
    def list_sessions(self) -> list[Session]:
        rc, out, err = self.run(self._rpc_argv("workspace.list", {}))
        if rc != 0:
            raise RuntimeError(f"cmux workspace.list failed: {err.strip() or rc}")
        surfaces_by_ws: dict[str, str] = {}
        for w in json.loads(out).get("workspaces", []):
            ws_id = w.get("id", "")
            if not ws_id:
                continue
            src, sout, _ = self.run(self._rpc_argv("surface.list", {"workspace_id": ws_id}))
            surfaces_by_ws[ws_id] = sout if src == 0 else '{"surfaces": []}'
        return build_sessions(out, surfaces_by_ws)

    def read_surface_text(self, surface: str) -> str:
        """Full visible text of a surface's screen (empty on any failure)."""
        rc, out, err = self.run(self._read_argv(surface))
        if rc != 0 or not out.strip():
            return ""
        try:
            return json.loads(out).get("text", "") or ""
        except (ValueError, AttributeError):
            return ""

    def read_surface(self, surface: str) -> str:
        """Last non-empty line of a surface's screen — the board's status line."""
        return _last_nonempty(self.read_surface_text(surface))

    def route(self, number: int, text: str) -> str:
        """Focus session `number`, type `text` verbatim, and submit (Enter).

        Focuses first so the target pane pops to the front and the user watches
        the command appear; sends text then a separate Enter key. Returns the
        target surface UUID. Raises KeyError if the number is unknown.
        """
        surface = resolve(number, self.list_sessions())
        if not surface:
            raise KeyError(f"no session numbered {number}")
        self.run(self._focus_argv(surface))
        self.run(self._send_text_argv(surface, text))
        self.run(self._send_key_argv(surface, "Enter"))
        return surface

    def read_status(self, number: int) -> str:
        """Return the last non-empty line of session `number`'s pane."""
        surface = resolve(number, self.list_sessions())
        if not surface:
            raise KeyError(f"no session numbered {number}")
        return self.read_surface(surface)
