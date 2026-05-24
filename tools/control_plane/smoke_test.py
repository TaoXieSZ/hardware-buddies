"""Safe real-cmux smoke test for the routing core.

Creates a THROWAWAY workspace, routes a unique marker into its terminal pane,
reads it back, asserts the marker appears, then closes ONLY that workspace.
Never sends to or closes any pre-existing workspace — all operations target the
throwaway's own surface/workspace UUID.

Run:  python -m control_plane.smoke_test   (or python tools/control_plane/smoke_test.py)
Skips cleanly (exit 0) when cmux is not installed.
"""
from __future__ import annotations

import json
import os
import random
import shutil
import string
import sys
import time

# Allow `python tools/control_plane/smoke_test.py` (add tools/ to path).
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from control_plane.cmux_control import CmuxClient, DEFAULT_CMUX  # noqa: E402


def _workspace_ids(client) -> set[str]:
    rc, out, _ = client.run([client.binary, "rpc", "workspace.list", "{}"])
    if rc != 0:
        return set()
    return {w.get("id", "") for w in json.loads(out).get("workspaces", []) if w.get("id")}


def main() -> int:
    cmux = shutil.which("cmux") or DEFAULT_CMUX
    if not os.path.exists(cmux):
        print("SKIP: cmux not installed (no /Applications/cmux.app and not on PATH)")
        return 0

    client = CmuxClient(binary=cmux)
    suffix = "".join(random.choices(string.ascii_uppercase + string.digits, k=8))
    marker = f"SPIKE_MARK_{suffix}"
    name = f"CPSPIKE_{suffix}"

    before_surf = {s.surface for s in client.list_sessions()}
    before_ws = _workspace_ids(client)

    rc, out, err = client.run([cmux, "new-workspace", "--name", name, "--cwd", "/tmp"])
    if rc != 0:
        print(f"FAIL: could not create throwaway workspace: {err.strip() or rc}")
        return 1
    time.sleep(1.0)

    new_ws = _workspace_ids(client) - before_ws
    if not new_ws:
        print("FAIL: throwaway workspace not found after create")
        return 1
    ws_id = next(iter(new_ws))

    # Find the throwaway's own terminal pane (a session in its workspace).
    target = next((s for s in client.list_sessions() if s.workspace == ws_id), None)
    if target is None:
        print("FAIL: throwaway terminal surface not found")
        return 1

    ok = False
    try:
        client.route(target.number, f"echo {marker}")  # focus + send + Enter
        time.sleep(1.5)
        ok = marker in client.read_surface_text(target.surface)  # full screen, not last line
    finally:
        # Close ONLY the throwaway, by its workspace UUID.
        client.run([cmux, "rpc", "workspace.close", json.dumps({"workspace_id": ws_id})])

    time.sleep(0.5)
    after_surf = {s.surface for s in client.list_sessions()}
    leaked = after_surf - before_surf
    if leaked:
        print(f"FAIL: throwaway not cleaned up, leaked surfaces {leaked}")
        return 1
    if after_surf != before_surf:
        print(f"FAIL: pre-existing surface set changed: {before_surf} -> {after_surf}")
        return 1

    print(f"PASS: routed+read-back {marker} into a throwaway pane; session list unchanged"
          if ok else "FAIL: marker not found in throwaway pane")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
