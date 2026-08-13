from __future__ import annotations

import copy

import pytest

from protocol_v2 import (
    BRIDGE_TO_DEVICE,
    DEVICE_TO_BRIDGE,
    AuthenticatedSession,
    ControlProtocolError,
    b64url_encode,
    make_proof,
    parse_secret,
    verify_proof,
)


SECRET = bytes(range(32))
DEVICE_NONCE = "dGVzdC1kZXZpY2Utbm9uY2U"
BRIDGE_NONCE = "dGVzdC1icmlkZ2Utbm9uY2U"
SESSION_ID = "session-test-001"


def test_fixed_mutual_authentication_vectors():
    bridge = make_proof(SECRET, "bridge", "watch-test", DEVICE_NONCE, BRIDGE_NONCE, SESSION_ID)
    device = make_proof(SECRET, "device", "watch-test", DEVICE_NONCE, BRIDGE_NONCE, SESSION_ID)

    assert bridge == "udFCXfbFJd3ksLvX3ODVJuUywLC3H4MEK3XU3ovn-l4"
    assert device == "xdxHfK7j-P6m_PENxrJonTozHxhKFFZSMK2c_LGrDH0"
    assert verify_proof(SECRET, bridge, "bridge", "watch-test", DEVICE_NONCE, BRIDGE_NONCE, SESSION_ID)
    assert verify_proof(SECRET, device, "device", "watch-test", DEVICE_NONCE, BRIDGE_NONCE, SESSION_ID)
    assert not verify_proof(SECRET, device, "bridge", "watch-test", DEVICE_NONCE, BRIDGE_NONCE, SESSION_ID)


def test_fixed_authenticated_envelope_vector_and_body_decode():
    device = AuthenticatedSession(SECRET, SESSION_ID, BRIDGE_TO_DEVICE, DEVICE_TO_BRIDGE)
    envelope = device.encode({"type": "command.decision", "command_id": "cmd-1", "decision": "approve"})

    assert envelope == {
        "session_id": SESSION_ID,
        "direction": DEVICE_TO_BRIDGE,
        "seq": 1,
        "body": "eyJjb21tYW5kX2lkIjoiY21kLTEiLCJkZWNpc2lvbiI6ImFwcHJvdmUiLCJ0eXBlIjoiY29tbWFuZC5kZWNpc2lvbiJ9",
        "mac": "xvpDQfW1S1wUr7hC7ePbRPF4-WAgcePv8LgRxoDBPsw",
    }
    bridge = AuthenticatedSession(SECRET, SESSION_ID, DEVICE_TO_BRIDGE, BRIDGE_TO_DEVICE)
    assert bridge.decode(envelope) == {"type": "command.decision", "command_id": "cmd-1", "decision": "approve"}


@pytest.mark.parametrize("field,value,code", [
    ("direction", BRIDGE_TO_DEVICE, "wrong_direction"),
    ("session_id", "other-session", "wrong_session"),
    ("mac", b64url_encode(b"x" * 32), "invalid_mac"),
])
def test_envelope_rejects_wrong_binding(field, value, code):
    sender = AuthenticatedSession(SECRET, SESSION_ID, BRIDGE_TO_DEVICE, DEVICE_TO_BRIDGE)
    envelope = sender.encode({"type": "task.snapshot", "task_id": "task-1"})
    envelope[field] = value
    receiver = AuthenticatedSession(SECRET, SESSION_ID, DEVICE_TO_BRIDGE, BRIDGE_TO_DEVICE)

    with pytest.raises(ControlProtocolError) as excinfo:
        receiver.decode(envelope)
    assert excinfo.value.code == code


def test_envelope_rejects_replay_and_out_of_order_without_advancing_on_bad_mac():
    sender = AuthenticatedSession(SECRET, SESSION_ID, BRIDGE_TO_DEVICE, DEVICE_TO_BRIDGE)
    first = sender.encode({"type": "one"})
    second = sender.encode({"type": "two"})
    receiver = AuthenticatedSession(SECRET, SESSION_ID, DEVICE_TO_BRIDGE, BRIDGE_TO_DEVICE)

    bad_second = copy.deepcopy(second)
    bad_second["mac"] = b64url_encode(b"x" * 32)
    with pytest.raises(ControlProtocolError, match="authentication"):
        receiver.decode(bad_second)
    assert receiver.inbound_sequence == 0
    assert receiver.decode(first) == {"type": "one"}
    with pytest.raises(ControlProtocolError) as replay:
        receiver.decode(first)
    assert replay.value.code == "replay"
    assert receiver.decode(second) == {"type": "two"}


def test_secret_must_be_exactly_32_bytes():
    assert parse_secret(b64url_encode(SECRET)) == SECRET
    assert parse_secret("") is None
    with pytest.raises(ControlProtocolError) as excinfo:
        parse_secret(b64url_encode(b"short"))
    assert excinfo.value.code == "invalid_secret"
