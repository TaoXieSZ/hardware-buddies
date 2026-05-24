"""Unit tests for the cmux routing core (tools/control_plane/cmux_control.py).

Sessions are cmux *surfaces* (terminal panes). Pure building (filter + numbering),
number→UUID resolution, and argv building are exercised with a mock runner — no
real cmux needed. JSON samples mirror the real `cmux rpc workspace.list` and
`cmux rpc surface.list` shapes observed on the dev machine.
"""

import json

from control_plane.cmux_control import (
    BOARD_MARKER,
    CmuxClient,
    Session,
    build_sessions,
    resolve,
)

# Two workspaces; the first holds the board pane, a Claude Code pane, and the
# voice-agent browser; the second holds a plain shell.
WS = json.dumps(
    {
        "workspaces": [
            {"id": "WSA", "ref": "workspace:1", "index": 0, "selected": True,
             "current_directory": "/Users/txie/proj/a"},
            {"id": "WSB", "ref": "workspace:2", "index": 1, "selected": False,
             "current_directory": "/tmp"},
        ]
    }
)
SURF = {
    "WSA": json.dumps({"surfaces": [
        {"id": "SB", "ref": "surface:20", "index": 0, "type": "terminal",
         "title": "cd '/x/tools' && python3 -m control_plane.board --watch",
         "focused": False},
        {"id": "S1", "ref": "surface:22", "index": 1, "type": "terminal",
         "title": "claude-desktop-buddy · 229a873b", "focused": True},
        {"id": "SBR", "ref": "surface:21", "index": 2, "type": "browser",
         "title": "Talk to your voice agent | Agora", "focused": False},
    ]}),
    "WSB": json.dumps({"surfaces": [
        {"id": "S2", "ref": "surface:30", "index": 0, "type": "terminal",
         "title": "txie@host:/tmp", "focused": False},
    ]}),
}


# ─── build_sessions ────────────────────────────────────────────────────

def test_build_numbers_terminals_across_workspaces():
    s = build_sessions(WS, SURF)
    assert [x.number for x in s] == [1, 2]
    assert [x.surface for x in s] == ["S1", "S2"]


def test_build_excludes_board_and_browser():
    surfaces = {x.surface for x in build_sessions(WS, SURF)}
    assert "SB" not in surfaces   # board pane (BOARD_MARKER in title)
    assert "SBR" not in surfaces  # browser surface (voice agent)
    assert BOARD_MARKER == "control_plane.board"


def test_build_carries_workspace_cwd_and_focus():
    s = build_sessions(WS, SURF)
    assert s[0].cwd == "/Users/txie/proj/a"  # owning workspace's dir
    assert s[0].workspace == "WSA"
    assert s[0].focused is True and s[0].selected is True  # selected aliases focused
    assert s[1].cwd == "/tmp"
    assert s[1].focused is False


def test_focus_only_in_selected_workspace():
    # A pane can be `focused` within its own (background) workspace; only the
    # one in the SELECTED workspace is the globally active pane.
    ws = json.dumps({"workspaces": [
        {"id": "SEL", "index": 0, "selected": True, "current_directory": "/s"},
        {"id": "BG", "index": 1, "selected": False, "current_directory": "/b"},
    ]})
    surf = {
        "SEL": json.dumps({"surfaces": [
            {"id": "sel1", "index": 0, "type": "terminal", "title": "a", "focused": True}]}),
        "BG": json.dumps({"surfaces": [
            {"id": "bg1", "index": 0, "type": "terminal", "title": "b", "focused": True}]}),
    }
    s = build_sessions(ws, surf)
    assert [(x.surface, x.focused) for x in s] == [("sel1", True), ("bg1", False)]


def test_build_orders_by_index_not_array_order():
    ws = json.dumps({"workspaces": [
        {"id": "B", "index": 1, "current_directory": "/b"},
        {"id": "A", "index": 0, "current_directory": "/a"},
    ]})
    surf = {
        "A": json.dumps({"surfaces": [
            {"id": "a1", "index": 0, "type": "terminal", "title": "a"}]}),
        "B": json.dumps({"surfaces": [
            {"id": "b1", "index": 0, "type": "terminal", "title": "b"}]}),
    }
    s = build_sessions(ws, surf)
    assert [(x.number, x.surface, x.cwd) for x in s] == [(1, "a1", "/a"), (2, "b1", "/b")]


def test_build_empty():
    assert build_sessions(json.dumps({"workspaces": []}), {}) == []


# ─── resolve ───────────────────────────────────────────────────────────

def test_resolve_hit():
    assert resolve(2, build_sessions(WS, SURF)) == "S2"


def test_resolve_miss():
    assert resolve(9, build_sessions(WS, SURF)) is None


# ─── CmuxClient with mock runner ───────────────────────────────────────

class MockRunner:
    """Answers workspace.list / surface.list / surface.read_text; records argv."""

    def __init__(self, screen=""):
        self.calls = []
        self.screen = screen

    def __call__(self, argv):
        self.calls.append(list(argv))
        method = argv[2] if len(argv) > 2 and argv[1] == "rpc" else ""
        if method == "workspace.list":
            return 0, WS, ""
        if method == "surface.list":
            ws_id = json.loads(argv[3]).get("workspace_id")
            return 0, SURF.get(ws_id, '{"surfaces": []}'), ""
        if method == "surface.read_text":
            return 0, json.dumps({"text": self.screen}), ""
        return 0, "", ""  # focus / send_text / send_key


def _method_call(calls, method):
    return next(c for c in calls if len(c) > 2 and c[1] == "rpc" and c[2] == method)


def test_route_builds_surface_argv_focus_send_enter():
    m = MockRunner()
    c = CmuxClient(binary="CMUX", runner=m)
    surface = c.route(1, "run the tests")
    assert surface == "S1"
    assert _method_call(m.calls, "surface.focus") == \
        ["CMUX", "rpc", "surface.focus", json.dumps({"surface_id": "S1"})]
    assert _method_call(m.calls, "surface.send_text") == \
        ["CMUX", "rpc", "surface.send_text",
         json.dumps({"surface_id": "S1", "text": "run the tests"})]
    assert _method_call(m.calls, "surface.send_key") == \
        ["CMUX", "rpc", "surface.send_key",
         json.dumps({"surface_id": "S1", "key": "Enter"})]


def test_route_unknown_number_raises_and_sends_nothing():
    m = MockRunner()
    c = CmuxClient(binary="CMUX", runner=m)
    try:
        c.route(9, "boom")
        assert False, "expected KeyError"
    except KeyError:
        pass
    assert all(not (len(call) > 2 and call[2] == "surface.send_text") for call in m.calls)


def test_route_verbatim_text_not_rewritten():
    m = MockRunner()
    c = CmuxClient(binary="CMUX", runner=m)
    payload = "git commit -m \"fix: 修复 bug\" && echo done"
    c.route(2, payload)
    send = _method_call(m.calls, "surface.send_text")
    assert json.loads(send[3])["text"] == payload  # exact, including CJK + quotes


def test_read_status_returns_last_nonempty_line():
    m = MockRunner(screen="line one\n\n  last meaningful line  \n\n")
    c = CmuxClient(binary="CMUX", runner=m)
    assert c.read_status(1) == "last meaningful line"


def test_read_surface_empty_on_blank():
    m = MockRunner(screen="   \n\n")
    c = CmuxClient(binary="CMUX", runner=m)
    assert c.read_surface("S1") == ""


# ─── board.build_board ─────────────────────────────────────────────────

def test_build_board_rows_from_client():
    from control_plane.board import build_board

    class FakeClient:
        def list_sessions(self):
            return build_sessions(WS, SURF)

        def read_surface(self, surface):
            return {"S1": "$ npm test", "S2": "ready"}.get(surface, "")

    rows = build_board(FakeClient())
    assert [r["number"] for r in rows] == [1, 2]
    assert rows[0]["status"] == "$ npm test"
    assert rows[0]["selected"] is True
    assert rows[0]["title"] == "claude-desktop-buddy · 229a873b"
    assert rows[1]["cwd"] == "/tmp"


def test_build_board_tolerates_read_error():
    from control_plane.board import build_board

    class FlakyClient:
        def list_sessions(self):
            return build_sessions(WS, SURF)

        def read_surface(self, surface):
            raise RuntimeError("boom")

    rows = build_board(FlakyClient())
    assert all(r["status"] == "" for r in rows)  # errors degrade to empty
