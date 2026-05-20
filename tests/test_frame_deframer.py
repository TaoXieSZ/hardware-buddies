"""Tests for buddy_core.frame_deframer.

The daemon's frame-ingest server receives a length-prefixed JPEG stream from
the StackChan (`uint32 LE length` + JPEG bytes). TCP `recv` returns arbitrary
byte chunks — headers and payloads can split across recvs. The deframer is a
stateful pure-logic accumulator that turns an arbitrary byte stream into a
sequence of complete JPEG payloads. No network code here — that test stays
host-friendly. See openspec/changes/2026-05-15-0003-stackchan-camera-gestures/.
"""

import struct

import pytest

from buddy_core.frame_deframer import MAX_FRAME, FrameDeframer


def _frame(payload: bytes) -> bytes:
    """Build a wire-format frame the way the firmware would."""
    return struct.pack("<I", len(payload)) + payload


def test_empty_feed_returns_no_frames():
    df = FrameDeframer()
    assert df.feed(b"") == []


def test_single_complete_frame():
    df = FrameDeframer()
    out = df.feed(_frame(b"hello"))
    assert out == [b"hello"]


def test_two_frames_in_one_feed():
    df = FrameDeframer()
    out = df.feed(_frame(b"AAA") + _frame(b"BBBB"))
    assert out == [b"AAA", b"BBBB"]


def test_header_split_across_feeds():
    df = FrameDeframer()
    raw = _frame(b"hello")
    # Split the 4-byte header in the middle.
    assert df.feed(raw[:2]) == []
    assert df.feed(raw[2:]) == [b"hello"]


def test_payload_split_across_feeds():
    df = FrameDeframer()
    raw = _frame(b"hello world")
    # Header + first 3 payload bytes, then the rest.
    assert df.feed(raw[:7]) == []
    assert df.feed(raw[7:]) == [b"hello world"]


def test_byte_at_a_time_eventually_yields_frame():
    """Worst-case fragmentation: feed one byte at a time."""
    df = FrameDeframer()
    raw = _frame(b"xy")
    out = []
    for i in range(len(raw)):
        out.extend(df.feed(raw[i:i + 1]))
    assert out == [b"xy"]


def test_leftover_bytes_carry_to_next_feed():
    """A complete frame followed by a partial header keeps the partial."""
    df = FrameDeframer()
    raw = _frame(b"first") + _frame(b"second")
    # Stop 2 bytes into the second frame's header.
    cut = len(_frame(b"first")) + 2
    assert df.feed(raw[:cut]) == [b"first"]
    assert df.feed(raw[cut:]) == [b"second"]


def test_zero_length_frame_is_yielded_as_empty_payload():
    """A 4-byte LE zero header with no payload is a valid empty frame."""
    df = FrameDeframer()
    out = df.feed(struct.pack("<I", 0))
    assert out == [b""]


def test_little_endian_length_decoding():
    """Catches accidental big-endian regressions in the deframer."""
    df = FrameDeframer()
    payload = b"x" * 0x0102  # 258 bytes
    out = df.feed(_frame(payload))
    assert out == [payload]


def test_oversized_length_header_raises():
    """A length header above the cap is corrupt/hostile — must raise rather
    than buffer unboundedly toward OOM."""
    df = FrameDeframer()
    # Claim a frame far bigger than the cap, but send only the 4-byte header.
    huge = struct.pack("<I", MAX_FRAME + 1)
    with pytest.raises(ValueError):
        df.feed(huge)


def test_oversized_length_does_not_grow_toward_claim():
    """The header is rejected at parse time, so the buffer only ever holds the
    bytes actually received — never an allocation proportional to the claimed
    (here 4 GiB) size."""
    df = FrameDeframer()
    sent = struct.pack("<I", 0xFFFFFFFF) + b"garbage"
    with pytest.raises(ValueError):
        df.feed(sent)
    # Buffer holds at most what was fed — not the 4 GiB the header claimed.
    assert len(df._buf) == len(sent)


def test_custom_max_frame_boundary():
    """A frame exactly at the cap is accepted; one byte over raises."""
    df = FrameDeframer(max_frame=16)
    assert df.feed(_frame(b"x" * 16)) == [b"x" * 16]
    with pytest.raises(ValueError):
        df.feed(struct.pack("<I", 17))
