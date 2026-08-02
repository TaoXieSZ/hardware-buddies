"""Tests for tools/cc-bridge/hook.py --agent injection (kimi-session-identity)."""

import importlib.util
import json
import sys
from pathlib import Path

_HOOK_PATH = Path(__file__).resolve().parents[1] / "tools" / "cc-bridge" / "hook.py"
_spec = importlib.util.spec_from_file_location("cc_hook", _HOOK_PATH)
hook = importlib.util.module_from_spec(_spec)
sys.modules["cc_hook"] = hook
_spec.loader.exec_module(hook)


def test_inject_agent_adds_field():
    out = hook._inject_agent(b'{"hook_event_name":"Stop","session_id":"s1"}', "kimi")
    obj = json.loads(out)
    assert obj["agent"] == "kimi"
    assert obj["hook_event_name"] == "Stop"


def test_inject_agent_empty_returns_verbatim():
    raw = b'{"a": 1}'
    assert hook._inject_agent(raw, "") is raw


def test_inject_agent_unparseable_returns_verbatim():
    raw = b"not json"
    assert hook._inject_agent(raw, "kimi") is raw


def test_inject_agent_non_dict_returns_verbatim():
    raw = b"[1, 2]"
    assert hook._inject_agent(raw, "kimi") is raw
