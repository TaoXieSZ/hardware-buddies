"""Demo the voice secretary's routing path WITHOUT voice or camera.

Stands in: (number, text) = what the ConvoAI tool would extract from your
speech; auto-confirm = the thumbs-up gesture. Exercises the real CmuxClient +
RouteStager against a THROWAWAY cmux workspace (created + closed here), so no
real session is touched.

  python tools/control_plane/demo.py
"""
from __future__ import annotations

import json
import os
import shutil
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from control_plane.board import build_board, render_text  # noqa: E402
from control_plane.cmux_control import CmuxClient, DEFAULT_CMUX  # noqa: E402
from control_plane.stager import RouteStager  # noqa: E402


def main() -> int:
    cmux = shutil.which("cmux") or DEFAULT_CMUX
    if not os.path.exists(cmux):
        print("cmux not installed — skipping demo")
        return 0
    client = CmuxClient(binary=cmux)

    print("1) Creating a throwaway session 'SECRETARY_DEMO' (your real ones are untouched)…")
    client.run([cmux, "new-workspace", "--name", "SECRETARY_DEMO", "--cwd", "/tmp"])
    time.sleep(1.0)

    target = next((s for s in client.list_sessions() if s.title == "SECRETARY_DEMO"), None)
    if target is None:
        print("   could not create throwaway; aborting")
        return 1

    print("\n2) The board the secretary sees (numbered — you'd say the number):\n")
    print(render_text(build_board(client)))

    cmd = 'echo "🤖 secretary routed this at $(date +%H:%M:%S)"'
    print(f"\n3) Voice (stand-in): \"session {target.number}, run: {cmd}\"")
    stager = RouteStager(route_fn=client.route)
    stager.stage(target.number, cmd)
    pending = stager.peek()
    print(f"   STAGED → [{pending.number}] {pending.text!r}  (nothing sent yet)")

    print("\n4) 👍 thumbs-up (stand-in: auto-confirm) → committing…")
    stager.confirm()
    time.sleep(1.5)

    print("\n5) That session's screen now (read-screen) — it ran:\n")
    rc, screen, _ = client.run(
        [cmux, "read-screen", "--workspace", target.uuid, "--lines", "8"]
    )
    print("   " + "\n   ".join(l for l in screen.splitlines() if l.strip()))

    print("\n6) Cleaning up the throwaway…")
    client.run([cmux, "rpc", "workspace.close", json.dumps({"workspace_id": target.uuid})])
    print("   done.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
