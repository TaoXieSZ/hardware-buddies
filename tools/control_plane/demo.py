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


def _workspace_ids(client) -> set[str]:
    rc, out, _ = client.run([client.binary, "rpc", "workspace.list", "{}"])
    if rc != 0:
        return set()
    return {w.get("id", "") for w in json.loads(out).get("workspaces", []) if w.get("id")}


def main() -> int:
    cmux = shutil.which("cmux") or DEFAULT_CMUX
    if not os.path.exists(cmux):
        print("cmux not installed — skipping demo")
        return 0
    client = CmuxClient(binary=cmux)

    print("1) Creating a throwaway session 'SECRETARY_DEMO' (your real ones are untouched)…")
    before_ws = _workspace_ids(client)
    client.run([cmux, "new-workspace", "--name", "SECRETARY_DEMO", "--cwd", "/tmp"])
    time.sleep(1.0)

    new_ws = _workspace_ids(client) - before_ws
    if not new_ws:
        print("   could not create throwaway; aborting")
        return 1
    ws_id = next(iter(new_ws))
    target = next((s for s in client.list_sessions() if s.workspace == ws_id), None)
    if target is None:
        print("   throwaway terminal surface not found; aborting")
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

    print("\n5) That pane's screen now — it ran:\n")
    screen = client.read_surface_text(target.surface)
    lines = [l for l in screen.splitlines() if l.strip()][-8:]
    print("   " + "\n   ".join(lines) if lines else "   (no output read)")

    print("\n6) Cleaning up the throwaway…")
    client.run([cmux, "rpc", "workspace.close", json.dumps({"workspace_id": ws_id})])
    print("   done.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
