"""Authenticated protocol-v2 control envelopes for the StopWatch bridge."""
from __future__ import annotations

import base64
import hashlib
import hmac
import json
import secrets
from dataclasses import dataclass
from typing import Any


PROTOCOL_V1 = 1
PROTOCOL_V2 = 2
SECRET_BYTES = 32
NONCE_BYTES = 24
MAX_SEQUENCE = (1 << 64) - 1
MAX_DEVICE_ID = 64
MAX_SESSION_ID = 64
MAX_COMMAND_ID = 64
MAX_TASK_ID = 64
MAX_LABEL = 48
MAX_COMMAND_TEXT = 1024
MAX_PREVIEW = 240
MAX_SUMMARY = 512
MAX_ERROR_DETAIL = 160
MAX_CANDIDATES = 4
MAX_CONTROL_BODY_BYTES = 4096

DEVICE_TO_BRIDGE = "d2b"
BRIDGE_TO_DEVICE = "b2d"


class ControlProtocolError(ValueError):
    def __init__(self, code: str, message: str):
        super().__init__(message)
        self.code = code
        self.message = message


def b64url_encode(value: bytes) -> str:
    return base64.urlsafe_b64encode(value).rstrip(b"=").decode("ascii")


def b64url_decode(value: str) -> bytes:
    if not isinstance(value, str) or not value:
        raise ControlProtocolError("invalid_encoding", "missing base64url value")
    try:
        return base64.urlsafe_b64decode(value + "=" * (-len(value) % 4))
    except (ValueError, TypeError) as exc:
        raise ControlProtocolError("invalid_encoding", "invalid base64url value") from exc


def parse_secret(value: str | None) -> bytes | None:
    """Decode an exact 32-byte base64url secret; an empty value disables v2."""
    if not value:
        return None
    secret = b64url_decode(value.strip())
    if len(secret) != SECRET_BYTES:
        raise ControlProtocolError("invalid_secret", "control secret must decode to 32 bytes")
    return secret


def canonical_body(body: dict[str, Any]) -> bytes:
    encoded = json.dumps(body, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")
    if len(encoded) > MAX_CONTROL_BODY_BYTES:
        raise ControlProtocolError("body_too_large", "control body exceeds the protocol limit")
    return encoded


def envelope_mac_input(direction: str, session_id: str, sequence: int, body_b64: str) -> bytes:
    return f"walkie-v2\n{direction}\n{session_id}\n{sequence}\n{body_b64}".encode("ascii")


def auth_mac_input(role: str, device_id: str, device_nonce: str, bridge_nonce: str, session_id: str) -> bytes:
    return f"walkie-v2-auth\n{role}\n{device_id}\n{device_nonce}\n{bridge_nonce}\n{session_id}".encode("ascii")


def make_proof(secret: bytes, role: str, device_id: str, device_nonce: str, bridge_nonce: str, session_id: str) -> str:
    return b64url_encode(hmac.new(secret, auth_mac_input(role, device_id, device_nonce, bridge_nonce, session_id), hashlib.sha256).digest())


def verify_proof(secret: bytes, proof: str, role: str, device_id: str, device_nonce: str, bridge_nonce: str, session_id: str) -> bool:
    expected = make_proof(secret, role, device_id, device_nonce, bridge_nonce, session_id)
    return hmac.compare_digest(expected, proof)


def fresh_token(size: int = NONCE_BYTES) -> str:
    return b64url_encode(secrets.token_bytes(size))


@dataclass
class AuthenticatedSession:
    secret: bytes
    session_id: str
    inbound_direction: str
    outbound_direction: str
    inbound_sequence: int = 0
    outbound_sequence: int = 0

    def encode(self, body: dict[str, Any]) -> dict[str, Any]:
        if self.outbound_sequence >= MAX_SEQUENCE:
            raise ControlProtocolError("sequence_exhausted", "outbound sequence exhausted")
        self.outbound_sequence += 1
        body_b64 = b64url_encode(canonical_body(body))
        mac = b64url_encode(
            hmac.new(
                self.secret,
                envelope_mac_input(self.outbound_direction, self.session_id, self.outbound_sequence, body_b64),
                hashlib.sha256,
            ).digest()
        )
        return {
            "session_id": self.session_id,
            "direction": self.outbound_direction,
            "seq": self.outbound_sequence,
            "body": body_b64,
            "mac": mac,
        }

    def decode(self, envelope: dict[str, Any]) -> dict[str, Any]:
        if envelope.get("session_id") != self.session_id:
            raise ControlProtocolError("wrong_session", "control envelope belongs to another session")
        if envelope.get("direction") != self.inbound_direction:
            raise ControlProtocolError("wrong_direction", "control envelope direction is invalid")
        sequence = envelope.get("seq")
        if not isinstance(sequence, int) or isinstance(sequence, bool) or sequence <= self.inbound_sequence:
            raise ControlProtocolError("replay", "control envelope sequence is not increasing")
        if sequence > MAX_SEQUENCE:
            raise ControlProtocolError("invalid_sequence", "control envelope sequence is out of range")
        body_b64 = envelope.get("body")
        mac = envelope.get("mac")
        if not isinstance(body_b64, str) or not isinstance(mac, str):
            raise ControlProtocolError("invalid_envelope", "control envelope is incomplete")
        expected = b64url_encode(
            hmac.new(
                self.secret,
                envelope_mac_input(self.inbound_direction, self.session_id, sequence, body_b64),
                hashlib.sha256,
            ).digest()
        )
        if not hmac.compare_digest(expected, mac):
            raise ControlProtocolError("invalid_mac", "control envelope authentication failed")
        raw = b64url_decode(body_b64)
        if len(raw) > MAX_CONTROL_BODY_BYTES:
            raise ControlProtocolError("body_too_large", "control body exceeds the protocol limit")
        try:
            body = json.loads(raw.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise ControlProtocolError("invalid_body", "control body is not valid UTF-8 JSON") from exc
        if not isinstance(body, dict):
            raise ControlProtocolError("invalid_body", "control body must be an object")
        self.inbound_sequence = sequence
        return body
