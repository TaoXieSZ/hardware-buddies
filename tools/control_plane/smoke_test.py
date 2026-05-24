"""Safe real-cmux smoke test for the routing core.

Creates a THROWAWAY workspace, routes a unique marker into it, reads it back,
asserts the marker appears, then closes ONLY that workspace. Never sends to or
closes any pre-existing/non-spike workspace — all session operations target the
throwaway's own UUID.

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


def main() -> int:
    cmux = shutil.which("cmux") or DEFAULT_CMUX
    if not os.path.exists(cmux):
        print("SKIP: cmux not installed (no /Applications/cmux.app and not on PATH)")
        return 0

    client = CmuxClient(binary=cmux)
    suffix = "".join(random.choices(string.ascii_uppercase + string.digits, k=8))
    marker = f"SPIKE_MARK_{suffix}"
    name = f"CPSPIKE_{suffix}"

    before = {s.uuid for s in client.list_sessions()}

    rc, out, err = client.run([cmux, "new-workspace", "--name", name, "--cwd", "/tmp"])
    if rc != 0:
        print(f"FAIL: could not create throwaway workspace: {err.strip() or rc}")
        return 1
    time.sleep(1.0)

    target = next((s for s in client.list_sessions() if s.title == name), None)
    if target is None:
        print("FAIL: throwaway workspace not found after create")
        return 1

    ok = False
    try:
        # Operate strictly on the throwaway's own UUID.
        client.send_text(target.uuid, f"echo {marker}")
        client.send_enter(target.uuid)
        time.sleep(1.5)
        rc, screen, _ = client.run(
            [cmux, "read-screen", "--workspace", target.uuid, "--lines", "15"]
        )
        ok = marker in screen
    finally:
        # Close ONLY the throwaway, by its UUID.
        client.run(
            [cmux, "rpc", "workspace.close", json.dumps({"workspace_id": target.uuid})]
        )

    time.sleep(0.5)
    after = {s.uuid for s in client.list_sessions()}
    leaked = (after - before)
    if leaked:
        print(f"FAIL: throwaway not cleaned up, leaked {leaked}")
        return 1
    if after != before:
        print(f"FAIL: pre-existing workspace set changed: {before} -> {after}")
        return 1

    print(f"PASS: routed+read-back {marker} into a throwaway; workspace list unchanged"
          if ok else "FAIL: marker not found in throwaway screen")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
