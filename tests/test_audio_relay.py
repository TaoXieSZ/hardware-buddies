"""Tests for the Path A2 audio relay packetizer (tools/audio-relay/relay.py).

The relay dir is hyphenated (matches cc-bridge/cursor-bridge), so it is not a
regular importable package — load relay.py by path, mirroring conftest's
approach for the bridge daemons. Only the pure helpers (header, packetize) are
exercised here; they must stay byte-compatible with src/stackchan/
audio_packet.h, which the native C++ tests pin from the device side.
"""

import importlib.util
from pathlib import Path

import pytest

_RELAY = Path(__file__).resolve().parent.parent / "tools" / "audio-relay" / "relay.py"


def _load_relay():
    spec = importlib.util.spec_from_file_location("audio_relay_relay", _RELAY)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


relay = _load_relay()

MAGIC = b"\xA5\xC3"
MAX_PAYLOAD = 640


# ─── header ────────────────────────────────────────────────────────────

def test_header_magic_and_length():
    h = relay.header(0)
    assert len(h) == 4
    assert h[0] == 0xA5
    assert h[1] == 0xC3


def test_header_seq_little_endian():
    h = relay.header(0x1234)
    assert h[2] == 0x34  # low byte first
    assert h[3] == 0x12


def test_header_seq_max():
    h = relay.header(0xFFFF)
    assert h[2] == 0xFF
    assert h[3] == 0xFF


def test_header_seq_wraps_past_16bit():
    # 0x10000 masks to 0x0000 — matches the device uint16 seq.
    assert relay.header(0x10000) == relay.header(0)


# ─── packetize ─────────────────────────────────────────────────────────

def test_packetize_empty_input():
    datagrams, next_seq = relay.packetize(b"", 5)
    assert datagrams == []
    assert next_seq == 5


def test_packetize_single_full_chunk():
    pcm = b"\x01" * MAX_PAYLOAD
    datagrams, next_seq = relay.packetize(pcm, 0)
    assert len(datagrams) == 1
    assert next_seq == 1
    assert datagrams[0][:2] == MAGIC
    assert datagrams[0][4:] == pcm  # payload after 4-byte header
    assert len(datagrams[0]) == 4 + MAX_PAYLOAD


def test_packetize_splits_oversized_chunk():
    pcm = b"\x02" * (MAX_PAYLOAD + 1)
    datagrams, next_seq = relay.packetize(pcm, 0)
    assert len(datagrams) == 2
    assert next_seq == 2
    assert len(datagrams[0][4:]) == MAX_PAYLOAD
    assert len(datagrams[1][4:]) == 1


def test_packetize_seq_increments_per_datagram():
    pcm = b"\x03" * (MAX_PAYLOAD * 3)
    datagrams, next_seq = relay.packetize(pcm, 10)
    seqs = [dg[2] | (dg[3] << 8) for dg in datagrams]
    assert seqs == [10, 11, 12]
    assert next_seq == 13


def test_packetize_seq_wraps_at_65536():
    pcm = b"\x04" * (MAX_PAYLOAD * 2)
    datagrams, next_seq = relay.packetize(pcm, 0xFFFF)
    seqs = [dg[2] | (dg[3] << 8) for dg in datagrams]
    assert seqs == [0xFFFF, 0x0000]
    assert next_seq == 1


def test_text_packet_magic_and_utf8():
    pkt = relay.text_packet("hi")
    assert pkt[:2] == b"\xA5\xC4"
    assert pkt[2:] == b"hi"


def test_text_packet_chinese_utf8():
    pkt = relay.text_packet("你好")
    assert pkt[:2] == b"\xA5\xC4"
    assert pkt[2:].decode("utf-8") == "你好"


def test_text_packet_capped_to_mtu():
    pkt = relay.text_packet("x" * 5000)
    assert len(pkt) - 2 <= relay.MAX_TEXT


def test_packetize_all_payloads_within_bound():
    pcm = b"\x05" * (MAX_PAYLOAD * 4 + 17)
    datagrams, _ = relay.packetize(pcm, 0)
    for dg in datagrams:
        assert dg[:2] == MAGIC
        assert 0 < len(dg[4:]) <= MAX_PAYLOAD
    # reassembled payload equals the original PCM
    assert b"".join(dg[4:] for dg in datagrams) == pcm
