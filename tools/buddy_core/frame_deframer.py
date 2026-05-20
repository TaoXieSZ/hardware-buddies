"""Pure-logic deframer for the StackChan camera-stream wire format.

Frames arrive over TCP as `<uint32 LE length><JPEG bytes>`. TCP `recv` does
not preserve message boundaries — headers and payloads can split across
recv calls, and several frames can also arrive in one recv. This class
turns an arbitrary byte stream into a sequence of complete JPEG payloads,
keeping any incomplete tail for the next feed.

The matching firmware-side header builder is in
`src/stackchan/frame_framing.h::writeFrameLengthLE`. See the openspec
change `2026-05-15-0003-stackchan-camera-gestures` for the wire contract.

Pure logic only — no socket / no asyncio. The actual asyncio TCP server
wraps an instance of this class and feeds it whatever `reader.read()`
returns. Keeps the parsing host-testable without a network.
"""

from __future__ import annotations

import struct
from typing import List

_HEADER = struct.Struct("<I")
_HEADER_LEN = _HEADER.size  # 4

# Cap declared frame size. A QVGA JPEG is well under 64 KiB; 512 KiB is ample.
# Without this, a corrupt or hostile uint32 header (up to 4 GiB) makes feed()
# buffer forever waiting for a frame that never completes → daemon OOM.
MAX_FRAME = 512 * 1024


class FrameDeframer:
    """Length-prefix framing accumulator. Not thread-safe; one per stream."""

    __slots__ = ("_buf", "_max")

    def __init__(self, max_frame: int = MAX_FRAME) -> None:
        self._buf = bytearray()
        self._max = max_frame

    def feed(self, chunk: bytes) -> List[bytes]:
        """Append `chunk` to the internal buffer and return any complete
        JPEG payloads now available, in arrival order. Leftover bytes
        (partial header or partial payload) stay buffered for the next
        feed.

        Raises ValueError if a frame's declared length exceeds max_frame —
        the stream is corrupt or hostile and the caller should drop it."""
        if chunk:
            self._buf.extend(chunk)
        out: List[bytes] = []
        while True:
            if len(self._buf) < _HEADER_LEN:
                break
            (length,) = _HEADER.unpack_from(self._buf, 0)
            if length > self._max:
                raise ValueError(
                    f"frame length {length} exceeds max {self._max}"
                )
            total = _HEADER_LEN + length
            if len(self._buf) < total:
                break
            # bytes() to detach from the bytearray slice — callers store these.
            out.append(bytes(self._buf[_HEADER_LEN:total]))
            del self._buf[:total]
        return out
