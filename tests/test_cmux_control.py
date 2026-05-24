"""Unit tests for the cmux routing core (tools/control_plane/cmux_control.py).

Pure parsing + number→UUID resolution + argv building are exercised with a mock
runner — no real cmux needed. The JSON sample mirrors the real
`cmux rpc workspace.list '{}'` shape (keys: id, ref, index, title,
current_directory, selected) observed on the dev machine.
"""

import json

from control_plane.cmux_control import (
    CmuxClient,
    Session,
    parse_workspaces,
    resolve,
)

SAMPLE = json.dumps(
    {
        "window_ref": "window:1",
        "workspaces": [
            {
                "ref": "workspace:1",
                "id": "6049F80B-DABB-47EC-8E9A-C7A5253D0066",
                "index": 0,
                "title": "git clone https://github.rbx.com/Roblox/deployment.git",
                "current_directory": "/Users/txie/roblox-ghe",
                "selected": True,
            },
            {
                "ref": "workspace:2",
                "id": "7D67FF66-6CB4-40F2-A77E-A774A5644F19",
                "index": 1,
                "title": "SPIKE",
                "current_directory": "/tmp",
                "selected": False,
            },
        ],
    }
)


# ─── parse_workspaces ──────────────────────────────────────────────────

def test_parse_basic_fields():
    s = parse_workspaces(SAMPLE)
    assert [x.number for x in s] == [1, 2]
    assert s[0].uuid == "6049F80B-DABB-47EC-8E9A-C7A5253D0066"
    assert s[0].ref == "workspace:1"
    assert s[0].selected is True
    assert s[0].cwd == "/Users/txie/roblox-ghe"
    assert s[1].title == "SPIKE"
    assert s[1].selected is False


def test_parse_empty():
    assert parse_workspaces(json.dumps({"workspaces": []})) == []


def test_parse_number_from_index_not_order():
    # index drives the 1-based number even if the array is out of order.
    js = json.dumps(
        {"workspaces": [
            {"ref": "workspace:2", "id": "B", "index": 1, "title": "two"},
            {"ref": "workspace:1", "id": "A", "index": 0, "title": "one"},
        ]}
    )
    s = parse_workspaces(js)
    assert [(x.number, x.uuid) for x in s] == [(1, "A"), (2, "B")]


# ─── resolve ───────────────────────────────────────────────────────────

def test_resolve_hit():
    s = parse_workspaces(SAMPLE)
    assert resolve(2, s) == "7D67FF66-6CB4-40F2-A77E-A774A5644F19"


def test_resolve_miss():
    s = parse_workspaces(SAMPLE)
    assert resolve(9, s) is None


# ─── CmuxClient with mock runner ───────────────────────────────────────

class MockRunner:
    """Records argv calls; answers workspace.list with SAMPLE, else rc=0."""

    def __init__(self, screen=""):
        self.calls = []
        self.screen = screen

    def __call__(self, argv):
        self.calls.append(list(argv))
        if argv[1:3] == ["rpc", "workspace.list"]:
            return 0, SAMPLE, ""
        if "read-screen" in argv:
            return 0, self.screen, ""
        return 0, "", ""


def test_route_builds_correct_argv_with_uuid_and_enter():
    m = MockRunner()
    c = CmuxClient(binary="CMUX", runner=m)
    uuid = c.route(2, "run the tests")
    assert uuid == "7D67FF66-6CB4-40F2-A77E-A774A5644F19"
    # list -> send text (by UUID, verbatim) -> send-key Enter
    assert m.calls[0] == ["CMUX", "rpc", "workspace.list", "{}"]
    assert m.calls[1] == ["CMUX", "send", "--workspace", uuid, "run the tests"]
    assert m.calls[2] == ["CMUX", "send-key", "--workspace", uuid, "Enter"]


def test_route_unknown_number_raises_and_sends_nothing():
    m = MockRunner()
    c = CmuxClient(binary="CMUX", runner=m)
    try:
        c.route(9, "boom")
        assert False, "expected KeyError"
    except KeyError:
        pass
    # only the list call happened — no send to any session
    assert all(call[1] != "send" for call in m.calls)


def test_route_verbatim_text_not_rewritten():
    m = MockRunner()
    c = CmuxClient(binary="CMUX", runner=m)
    payload = "git commit -m \"fix: 修复 bug\" && echo done"
    c.route(1, payload)
    send = [c for c in m.calls if c[1] == "send"][0]
    assert send[-1] == payload  # exact, including CJK + quotes


def test_read_status_returns_last_nonempty_line():
    m = MockRunner(screen="line one\n\n  last meaningful line  \n\n")
    c = CmuxClient(binary="CMUX", runner=m)
    assert c.read_status(1) == "last meaningful line"


# ─── board.build_board ─────────────────────────────────────────────────

def test_build_board_rows_from_client():
    from control_plane.board import build_board

    class FakeClient:
        def list_sessions(self):
            return parse_workspaces(SAMPLE)

        def read_status(self, number):
            return {1: "$ npm test", 2: "ready"}.get(number, "")

    rows = build_board(FakeClient())
    assert [r["number"] for r in rows] == [1, 2]
    assert rows[0]["status"] == "$ npm test"
    assert rows[0]["selected"] is True
    assert rows[1]["title"] == "SPIKE"


def test_build_board_tolerates_read_status_error():
    from control_plane.board import build_board

    class FlakyClient:
        def list_sessions(self):
            return parse_workspaces(SAMPLE)

        def read_status(self, number):
            raise RuntimeError("boom")

    rows = build_board(FlakyClient())
    assert all(r["status"] == "" for r in rows)  # errors degrade to empty
