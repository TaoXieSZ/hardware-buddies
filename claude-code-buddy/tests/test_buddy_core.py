"""Tests for tools/buddy_core/core.py — the shared daemon library."""

import asyncio
import struct
import zlib


# ─── BuddyState.to_payload ─────────────────────────────────────────────

def test_to_payload_basic_fields(fresh_state):
    fresh_state.total = 2
    fresh_state.running = 1
    fresh_state.waiting = 0
    fresh_state.msg = "thinking…"
    fresh_state.tokens = 1234
    p = fresh_state.to_payload()
    assert p["total"] == 2
    assert p["running"] == 1
    assert p["waiting"] == 0
    assert p["msg"] == "thinking…"
    assert p["tokens"] == 1234


def test_to_payload_app_tag(fresh_state):
    # No tag by default → field omitted (back-compatible single-feed firmware).
    assert "app" not in fresh_state.to_payload()
    # Set → emitted on every heartbeat for dual-feed routing.
    fresh_state.app = "cursor"
    assert fresh_state.to_payload()["app"] == "cursor"
    assert fresh_state.to_payload()["app"] == "cursor"


def test_to_payload_prompt_omitted_when_none(fresh_state):
    assert "prompt" not in fresh_state.to_payload()
    fresh_state.prompt = {"id": "r1", "tool": "Bash", "hint": "rm -rf"}
    assert fresh_state.to_payload()["prompt"]["tool"] == "Bash"


def test_to_payload_carries_hud_fields(fresh_state):
    # HUD metrics are state, not one-shot — every heartbeat carries them.
    fresh_state.context_pct = 62
    fresh_state.model = "Opus 4.7"
    fresh_state.limit_5h = 38
    fresh_state.limit_7d = 13
    fresh_state.session_ms = 1234567
    p = fresh_state.to_payload()
    assert p["context_pct"] == 62
    assert p["model"] == "Opus 4.7"
    assert p["limit_5h"] == 38
    assert p["limit_7d"] == 13
    assert p["session_ms"] == 1234567
    # Still present (unchanged) on the next heartbeat.
    assert fresh_state.to_payload()["context_pct"] == 62


def test_to_payload_completed_is_one_shot(fresh_state):
    fresh_state.completed = True
    assert fresh_state.to_payload().get("completed") is True
    # Cleared after the first read — the firmware should only celebrate once.
    assert "completed" not in fresh_state.to_payload()


def test_to_payload_pending_play_is_one_shot(fresh_state):
    fresh_state.pending_play = "permissionrequest"
    assert fresh_state.to_payload()["play"] == "permissionrequest"
    assert "play" not in fresh_state.to_payload()


def test_to_payload_sessions_list_carries_sid_and_running(fresh_state):
    # Per-session list for the cardputer session switcher: sid (= Claude
    # session_id, matches cmux checkpoint_id) + running, insertion order kept.
    fresh_state._sessions = {
        "41af42bb-fb94-42d9-88bd-f03446d71f25": {"running": True},
        "9c2e0000-0000-4000-8000-000000000002": {"running": False},
    }
    assert fresh_state.to_payload()["sessions"] == [
        {"sid": "41af42bb-fb94-42d9-88bd-f03446d71f25", "running": True},
        {"sid": "9c2e0000-0000-4000-8000-000000000002", "running": False},
    ]


def test_to_payload_sessions_omitted_when_empty(fresh_state):
    # Back-compatible: no sessions → key absent, payload stays lean.
    assert "sessions" not in fresh_state.to_payload()


def test_to_payload_sessions_capped_to_protect_firmware_buffer(fresh_state):
    fresh_state._sessions = {f"sid-{i}": {"running": False} for i in range(30)}
    assert len(fresh_state.to_payload()["sessions"]) == 16


def test_to_payload_session_label_present_when_known(fresh_state):
    # cmux auto-name resolved → carried as `label` so firmware shows a name.
    fresh_state._sessions = {"sid-a": {"running": True}}
    fresh_state.session_labels = {"sid-a": "hardware-buddies-setup"}
    assert fresh_state.to_payload()["sessions"][0] == {
        "sid": "sid-a", "running": True, "label": "hardware-buddies-setup"}


def test_to_payload_session_label_omitted_when_unknown(fresh_state):
    # No label source at all → fall back to _sessions, key omitted, firmware
    # falls back to sid prefix.
    fresh_state._sessions = {"sid-a": {"running": False}}
    assert "label" not in fresh_state.to_payload()["sessions"][0]


def test_to_payload_sessions_from_cmux_snapshot(fresh_state):
    # When cmux labels are present, the list is the cmux snapshot (every
    # switchable session), not just hook-seen _sessions; running is filled from
    # _sessions when known, else False.
    fresh_state.session_labels = {"sid-a": "feat-a", "sid-b": "hi"}
    fresh_state._sessions = {"sid-a": {"running": True}}  # sid-b unseen by hooks
    assert fresh_state.to_payload()["sessions"] == [
        {"sid": "sid-a", "running": True, "label": "feat-a"},
        {"sid": "sid-b", "running": False, "label": "hi"},
    ]
    assert fresh_state.pending_play is None


# ─── BuddyState.add_entry ──────────────────────────────────────────────

def test_add_entry_newest_first(fresh_state):
    fresh_state.add_entry("first")
    fresh_state.add_entry("second")
    assert fresh_state.entries[0] == "second"
    assert fresh_state.entries[1] == "first"


def test_add_entry_truncates_to_91_chars(fresh_state):
    fresh_state.add_entry("x" * 200)
    assert len(fresh_state.entries[0]) == 91


def test_add_entry_caps_at_8(fresh_state):
    for i in range(20):
        fresh_state.add_entry(f"entry {i}")
    assert len(fresh_state.entries) == 8
    assert fresh_state.entries[0] == "entry 19"


# ─── _safe_set ─────────────────────────────────────────────────────────

def test_safe_set_is_idempotent(core):
    loop = asyncio.new_event_loop()
    try:
        fut = loop.create_future()
        core._safe_set(fut, "allow")
        assert fut.result() == "allow"
        # A redelivered BLE notification must not raise InvalidStateError.
        core._safe_set(fut, "deny")
        assert fut.result() == "allow"
    finally:
        loop.close()


# ─── _MOD_FLAGS ────────────────────────────────────────────────────────

def test_mod_flags_cover_modifier_keycodes(core):
    # The flag-mask modifier kVK codes (Caps Lock 57 is intentionally absent —
    # it isn't relayed as a CGEventFlagsChanged modifier).
    for kc in (54, 55, 56, 58, 59, 60, 61, 62, 63):
        assert kc in core._MOD_FLAGS
    assert 57 not in core._MOD_FLAGS
    # Right Option (61, the PTT default) is kCGEventFlagMaskAlternate.
    assert core._MOD_FLAGS[61] == 0x080000


# ─── keyboard relay: name → kVK (cmd:key) ──────────────────────────────

def test_kvk_for_special_keys(core):
    assert core.kvk_for("enter") == 0x24
    assert core.kvk_for("return") == 0x24
    assert core.kvk_for("backspace") == 0x33
    assert core.kvk_for("tab") == 0x30
    assert core.kvk_for("esc") == 0x35
    assert core.kvk_for("left") == 0x7B
    assert core.kvk_for("up") == 0x7E


def test_kvk_for_letters_and_digits(core):
    assert core.kvk_for("a") == 0x00
    assert core.kvk_for("c") == 0x08   # ⌘C lands on the real C keycode
    assert core.kvk_for("z") == 0x06
    assert core.kvk_for("1") == 0x12
    assert core.kvk_for("0") == 0x1D


def test_kvk_for_is_case_insensitive_and_unknown_is_none(core):
    assert core.kvk_for("UP") == 0x7E
    assert core.kvk_for("ENTER") == 0x24
    assert core.kvk_for("nope") is None
    assert core.kvk_for("") is None


def test_mod_mask_covers_relay_modifiers(core):
    assert core._MOD_MASK["cmd"] == 0x100000
    assert core._MOD_MASK["shift"] == 0x020000
    assert core._MOD_MASK["opt"] == 0x080000
    assert core._MOD_MASK["ctrl"] == 0x040000


# ─── screenshot PNG writer ─────────────────────────────────────────────

def test_rgb565_to_rgb888_pure_colors(core):
    # red 0xF800, green 0x07E0, blue 0x001F (little-endian byte pairs)
    raw = bytes([0x00, 0xF8, 0xE0, 0x07, 0x1F, 0x00])
    rgb = core._rgb565_to_rgb888(raw, 3, 1)
    assert rgb[0:3] == bytes([0xFF, 0x00, 0x00])   # red
    assert rgb[3:6] == bytes([0x00, 0xFF, 0x00])   # green
    assert rgb[6:9] == bytes([0x00, 0x00, 0xFF])   # blue


def test_write_png_is_valid(core, tmp_path):
    w, h = 4, 3
    rgb = bytes([10, 20, 30]) * (w * h)
    out = tmp_path / "shot.png"
    core._write_png(str(out), w, h, rgb)
    data = out.read_bytes()
    # PNG signature
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    # first chunk is IHDR with our dimensions
    assert data[12:16] == b"IHDR"
    iw, ih = struct.unpack(">II", data[16:24])
    assert (iw, ih) == (w, h)
    # IDAT decompresses to h rows of (1 filter byte + w*3)
    idat_start = data.find(b"IDAT") + 4
    idat_len = struct.unpack(">I", data[idat_start - 8:idat_start - 4])[0]
    raw = zlib.decompress(data[idat_start:idat_start + idat_len])
    assert len(raw) == h * (1 + w * 3)


# ─── BleWriter.write fast-skip ─────────────────────────────────────────

def test_ble_write_never_reconnects_inline(core):
    """write() must skip an offline peer instantly. The old inline
    ensure_connected() ran a BleakScanner.discover (SCAN_TIMEOUT=8s) and
    MultiBleWriter gathers all peers — one absent stick stalled every
    heartbeat emit (serial included), so permission prompts reached the
    Tab5 after the 8s approval window had burned. reconnect_loop owns
    reconnection."""
    w = core.BleWriter("Claude-TEST")
    w.client = None

    async def boom():
        raise AssertionError("write() must not call ensure_connected()")

    w.ensure_connected = boom
    loop = asyncio.new_event_loop()
    try:
        loop.run_until_complete(w.write({"running": 0}))   # returns, no scan
    finally:
        loop.close()


# ─── on_stick_line: selectSession dispatch ─────────────────────────────

def test_on_stick_line_select_session_dispatches_callback():
    """{"cmd":"selectSession"} fires the injected callback (off-thread) with
    the sid — backs the cardputer physical session switcher."""
    import json
    import logging
    import threading
    from buddy_core.core import make_on_stick_line

    got = {}
    done = threading.Event()

    def _cb(sid):
        got["sid"] = sid
        done.set()

    on_line, _pending = make_on_stick_line(
        61, "tap", logging.getLogger("test"), on_select_session=_cb)
    on_line(json.dumps({"cmd": "selectSession", "sid": "ABC"}))
    assert done.wait(2.0), "callback was not dispatched"
    assert got["sid"] == "ABC"


def test_to_payload_question_when_pending(fresh_state):
    fresh_state.pending_question = {
        "rid": "RID", "header": "H", "prompt": "pick", "multi": False,
        "options": [{"id": "opt0", "label": "A"}, {"id": "opt1", "label": "B"}]}
    q = fresh_state.to_payload()["question"]
    assert q["rid"] == "RID" and q["header"] == "H" and q["text"] == "pick"
    assert q["multi"] is False
    assert q["options"] == [{"id": "opt0", "label": "A"}, {"id": "opt1", "label": "B"}]


def test_to_payload_question_omitted_when_none(fresh_state):
    assert "question" not in fresh_state.to_payload()


def test_ext_sessions_merge_into_payload():
    # cursor-bridge ext_sessions append to sessions[] tagged agent=cursor;
    # local (claude) sessions stay untagged. (cardputer-cursor-sessions)
    import time
    from buddy_core.core import BuddyState
    st = BuddyState()
    st._sessions["claudeA"] = {"running": True}
    st.session_labels = {"claudeA": "alpha"}
    st.set_session_state("claudeA", "thinking")
    st.ext_sessions["cursor"] = {
        "sessions": [{"sid": "cur1", "label": "beta", "st": "tool", "running": True}],
        "ts": time.monotonic(),
    }
    sess = {s["sid"]: s for s in st.to_payload()["sessions"]}
    assert sess["claudeA"]["st"] == "thinking" and "agent" not in sess["claudeA"]
    assert sess["cur1"]["agent"] == "cursor" and sess["cur1"]["st"] == "tool"


def test_ext_sessions_stale_dropped():
    # A snapshot older than EXT_STALE_SEC is not emitted (cursor-bridge died).
    from buddy_core.core import BuddyState
    st = BuddyState()
    st._sessions["claudeA"] = {"running": True}
    st.ext_sessions["cursor"] = {
        "sessions": [{"sid": "cur1", "running": True}],
        "ts": 0.0,   # ancient → stale
    }
    sids = [s["sid"] for s in st.to_payload().get("sessions", [])]
    assert "cur1" not in sids and "claudeA" in sids


def test_ext_sessions_codex_merges_alongside_cursor():
    # The agent-keyed merge is agnostic: a third agent (codex) slots in with no
    # cc-bridge change. Its rows carry a `cwd` field (used by cc-bridge focus,
    # ignored by firmware) that must ride through to_payload untouched.
    # (cardputer-codex-sessions)
    import time
    from buddy_core.core import BuddyState
    st = BuddyState()
    st._sessions["claudeA"] = {"running": True}
    st.session_labels = {"claudeA": "alpha"}
    now = time.monotonic()
    st.ext_sessions["cursor"] = {
        "sessions": [{"sid": "cur1", "label": "beta", "st": "tool", "running": True}],
        "ts": now,
    }
    st.ext_sessions["codex"] = {
        "sessions": [{"sid": "019f0287-codex", "label": "proj-z", "st": "waiting",
                      "ws": 3, "running": True, "cwd": "/Users/txie/proj-z"}],
        "ts": now,
    }
    sess = {s["sid"]: s for s in st.to_payload()["sessions"]}
    assert sess["cur1"]["agent"] == "cursor"                 # cursor still tagged
    assert sess["019f0287-codex"]["agent"] == "codex"        # codex tagged
    assert sess["019f0287-codex"]["st"] == "waiting"
    assert sess["019f0287-codex"]["cwd"] == "/Users/txie/proj-z"  # cwd preserved


def test_on_stick_line_answer_question_dispatches_callback():
    import json
    import logging
    import threading
    from buddy_core.core import make_on_stick_line
    got = {}
    done = threading.Event()

    def cb(rid, ids, text):
        got["rid"] = rid
        got["ids"] = ids
        got["text"] = text
        done.set()

    on_line, _ = make_on_stick_line(
        61, "tap", logging.getLogger("test"), on_answer_question=cb)
    on_line(json.dumps({"cmd": "answerQuestion", "rid": "R", "ids": ["opt0", "opt1"]}))
    assert done.wait(2.0), "answerQuestion callback not dispatched"
    assert got["rid"] == "R" and got["ids"] == ["opt0", "opt1"] and got["text"] is None


def test_multi_question_sequential_accumulate_then_reply():
    # openspec cardputer-multi-question: device answers each sub-question via a
    # synthetic rid R#i; daemon accumulates and replies ONCE with all answers.
    import json
    import logging
    import threading
    from buddy_core.core import make_on_stick_line, BuddyState
    got = {}
    done = threading.Event()

    def cb(rid, ids, text, selections=None):
        got.update(rid=rid, ids=ids, text=text, selections=selections)
        done.set()

    st = BuddyState()
    st.mq = {"rid": "RID", "cur": 0, "answers": [], "subq": [
        {"header": "Q0", "options": [{"id": "a0", "label": "A0"}]},
        {"header": "Q1", "options": [{"id": "b0", "label": "B0"}]},
    ]}
    on_line, _ = make_on_stick_line(
        61, "tap", logging.getLogger("test"), state=st, on_answer_question=cb)

    # answer sub-question 0 → accumulate, NO reply yet
    on_line(json.dumps({"cmd": "answerQuestion", "rid": "RID#0", "ids": ["a0"]}))
    assert not done.is_set()
    assert st.mq["cur"] == 1 and st.mq["answers"] == ["A0"]

    # answer sub-question 1 → complete → one reply with both answers
    on_line(json.dumps({"cmd": "answerQuestion", "rid": "RID#1", "ids": ["b0"]}))
    assert done.wait(2.0), "final reply not dispatched"
    assert got["rid"] == "RID" and got["selections"] == ["A0", "B0"]
    assert st.mq is None   # state cleared after completing


def test_multi_question_stale_subindex_ignored():
    import json
    import logging
    from buddy_core.core import make_on_stick_line, BuddyState
    calls = []
    st = BuddyState()
    st.mq = {"rid": "RID", "cur": 1, "answers": ["A0"], "subq": [
        {"header": "Q0", "options": [{"id": "a0", "label": "A0"}]},
        {"header": "Q1", "options": [{"id": "b0", "label": "B0"}]},
    ]}
    on_line, _ = make_on_stick_line(
        61, "tap", logging.getLogger("test"), state=st,
        on_answer_question=lambda *a: calls.append(a))
    # answering #0 again (already past) → ignored, no state change
    on_line(json.dumps({"cmd": "answerQuestion", "rid": "RID#0", "ids": ["a0"]}))
    assert st.mq["cur"] == 1 and calls == []


def test_on_stick_line_answer_question_free_text_dispatches():
    # chat about it / cancel：固件回送 {rid, text}，回调收到 text、ids 为 None。
    import json
    import logging
    import threading
    from buddy_core.core import make_on_stick_line
    got = {}
    done = threading.Event()

    def cb(rid, ids, text):
        got.update(rid=rid, ids=ids, text=text)
        done.set()

    on_line, _ = make_on_stick_line(
        61, "tap", logging.getLogger("test"), on_answer_question=cb)
    on_line(json.dumps({"cmd": "answerQuestion", "rid": "R", "text": "先跳过，你来定"}))
    assert done.wait(2.0), "free-text answerQuestion callback not dispatched"
    assert got["rid"] == "R" and got["ids"] is None and got["text"] == "先跳过，你来定"


def test_per_session_state_in_payload():
    # 多 session 各自状态进 payload sessions[].st；聚合字段不变。
    from buddy_core.core import BuddyState
    st = BuddyState()
    st._sessions["A"] = {"running": True}
    st._sessions["B"] = {"running": True}
    st.session_labels = {"A": "alpha", "B": "beta"}
    st.set_session_state("A", "thinking")
    st.set_session_state("B", "tool")
    p = st.to_payload()
    sess = {s["sid"]: s for s in p["sessions"]}
    assert sess["A"]["st"] == "thinking" and sess["A"]["label"] == "alpha"
    assert sess["B"]["st"] == "tool"
    assert "total" in p and "running" in p and "msg" in p  # 聚合保留


def test_waiting_assigns_fifo_seq():
    # 进入 waiting 分配单调递增 ws；离开清零；重入拿更大的 seq。
    from buddy_core.core import BuddyState
    st = BuddyState()
    st.set_session_state("A", "waiting")   # A 先等
    st.set_session_state("B", "waiting")   # B 后等
    assert 0 < st._sessions["A"]["ws"] < st._sessions["B"]["ws"]   # FIFO
    st.set_session_state("A", "tool")      # A 离开等待
    assert st._sessions["A"]["ws"] == 0
    prev_b = st._sessions["B"]["ws"]
    st.set_session_state("A", "waiting")   # A 重入 → 更大 seq（排到 B 之后）
    assert st._sessions["A"]["ws"] > prev_b


def test_on_stick_line_select_session_no_callback_is_safe():
    """No on_select_session wired (e.g. cursor-bridge) → selectSession is a
    silent no-op, never raises."""
    import json
    import logging
    from buddy_core.core import make_on_stick_line

    on_line, _ = make_on_stick_line(61, "tap", logging.getLogger("test"))
    on_line(json.dumps({"cmd": "selectSession", "sid": "ABC"}))  # must not raise


# ─── wait_permission: external-agent (Cursor) relay through cc-bridge ───────

class _FakeBle:
    """Permission-capable peer (no 'SC' in prefix → not short-circuited)."""
    def __init__(self, prefixes=("Claude-7AFD",)):
        self.connected_prefixes = list(prefixes)


class _FakePermWriter:
    def __init__(self):
        self.buf = bytearray()

    def write(self, b):
        self.buf.extend(b)

    async def drain(self):
        pass


def _drive_wait_permission(req):
    """Run _handle_wait_permission with a peer connected; a concurrent task
    plays 'the device' — once the prompt is up it asserts on state, then
    resolves the pending future with decision 'once'. Returns (state, decision,
    saw_prompt) where saw_prompt is the state.prompt captured while blocking."""
    import json
    import logging
    from buddy_core.core import BuddyState, _handle_wait_permission

    st = BuddyState()
    pending = {}
    w = _FakePermWriter()
    captured = {}

    async def _go():
        async def device():
            # Wait for the prompt to be surfaced + the future registered.
            for _ in range(200):
                if pending:
                    captured["prompt"] = dict(st.prompt) if st.prompt else None
                    captured["sessions_keys"] = list(st._sessions.keys())
                    rid = next(iter(pending))
                    pending[rid].get_loop().call_soon_threadsafe(
                        pending[rid].set_result, "once")
                    return
                await asyncio.sleep(0.001)

        await asyncio.gather(
            _handle_wait_permission(req, w, st, asyncio.Event(), pending,
                                    _FakeBle(), logging.getLogger("test")),
            device(),
        )

    asyncio.run(_go())
    reply = json.loads(bytes(w.buf).decode().strip())
    return st, reply.get("decision"), captured


def test_wait_permission_cursor_agent_does_not_pin_session():
    # A relayed Cursor permission (agent='cursor') must NOT mint a bucket in
    # cc-bridge's Claude _sessions — that phantom would inflate the reaper's
    # total. It surfaces the prompt (tagged agent) and routes the decision.
    st, decision, cap = _drive_wait_permission({
        "action": "wait_permission", "id": "cursor_ab12_99", "tool": "shell",
        "hint": "rm -rf /tmp/x", "timeout": 5.0,
        "agent": "cursor", "session_id": "ab12cd34-cursor-session",
    })
    assert decision == "once"
    assert cap["prompt"]["agent"] == "cursor"            # device can mark `cu`
    assert cap["prompt"]["tool"] == "shell"
    # the cursor sid never entered cc-bridge's session map (no phantom bucket)
    assert "ab12cd34-cursor-session" not in cap["sessions_keys"]
    assert st._sessions == {}                            # nothing lingered


def test_wait_permission_claude_still_pins_session():
    # Regression: a normal (no-agent) Claude permission STILL pins its session
    # so the cardputer's FIFO rotation pins the asking session as before.
    st, decision, cap = _drive_wait_permission({
        "action": "wait_permission", "id": "req_1", "tool": "Bash",
        "hint": "ls", "timeout": 5.0, "session_id": "claude-sid-1",
    })
    assert decision == "once"
    assert "agent" not in cap["prompt"]                  # untagged for own agent
    # the claude sid WAS pinned while waiting (bucket created)
    assert "claude-sid-1" in cap["sessions_keys"]
