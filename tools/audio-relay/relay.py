"""Audio relay for Path A2: browser PCM -> WebSocket -> sequenced UDP -> StackChan.

The Mac browser (buddy-voice frontend) taps the Agora agent's remote audio
track, downsamples it to 16 kHz mono signed-16-bit PCM, and streams raw PCM
chunks over a WebSocket to this relay. The relay splits each chunk into
MTU-safe UDP datagrams with a 4-byte header and forwards them to the
StackChan device, which plays them on its speaker.

Wire contract (must match src/stackchan/audio_packet.h):
    datagram = <magic 0xA5 0xC3><uint16-LE seq><PCM payload <=640 bytes>
    PCM = signed 16-bit little-endian, 16000 Hz, mono.

This is standalone (not part of the BLE buddy_core daemon) so the audio path
has zero blast radius on the daemon. Run it alongside the daemon.

Usage:
    pip install -r requirements.txt
    STACKCHAN_AUDIO_HOST=192.168.x.y python relay.py

Env:
    STACKCHAN_AUDIO_HOST   device IP for UDP audio (default 127.0.0.1)
    STACKCHAN_AUDIO_PORT   device UDP port        (default 5005)
    RELAY_WS_PORT          WebSocket listen port  (default 8771)
"""

from __future__ import annotations

import asyncio
import logging
import os
import socket

MAGIC = b"\xA5\xC3"        # audio (PCM) datagram
TEXT_MAGIC = b"\xA5\xC4"   # subtitle/caption datagram (UTF-8 text)
# 320 samples * 2 bytes = 640 = 20 ms @ 16 kHz mono. Datagram (644 B) stays
# under a 1500-byte MTU so it never fragments.
MAX_PAYLOAD = 640
# Cap caption bytes so the text datagram also stays under the MTU.
MAX_TEXT = 600

log = logging.getLogger("audio-relay")


def header(seq: int) -> bytes:
    """Build the 4-byte datagram header for `seq` (wraps at 65536)."""
    seq &= 0xFFFF
    return MAGIC + bytes([seq & 0xFF, (seq >> 8) & 0xFF])


def packetize(pcm: bytes, seq: int) -> tuple[list[bytes], int]:
    """Split raw PCM into header-prefixed datagrams.

    Returns (datagrams, next_seq). Each datagram is header(seq) + a payload of
    at most MAX_PAYLOAD bytes. `seq` increments per datagram and wraps at
    65536. Empty input yields no datagrams and an unchanged seq.
    """
    datagrams: list[bytes] = []
    for off in range(0, len(pcm), MAX_PAYLOAD):
        chunk = pcm[off : off + MAX_PAYLOAD]
        datagrams.append(header(seq) + chunk)
        seq = (seq + 1) & 0xFFFF
    return datagrams, seq


def text_packet(text: str) -> bytes:
    """Build a subtitle datagram: TEXT_MAGIC + UTF-8 text (capped to MTU)."""
    return TEXT_MAGIC + text.encode("utf-8")[:MAX_TEXT]


async def _handle(ws, dest: tuple[str, int], udp: socket.socket) -> None:
    seq = 0
    peer = getattr(ws, "remote_address", "?")
    log.info("ws client connected: %s", peer)
    try:
        async for message in ws:
            if isinstance(message, str):
                # subtitle/caption text → one text datagram
                if message:
                    udp.sendto(text_packet(message), dest)
                continue
            datagrams, seq = packetize(message, seq)
            for dg in datagrams:
                udp.sendto(dg, dest)
    except Exception as e:  # noqa: BLE001 - relay must survive any client
        log.warning("ws client error: %s", e)
    finally:
        log.info("ws client gone: %s", peer)


async def main() -> None:
    import websockets  # imported here so the pure helpers are testable w/o it

    host = os.environ.get("STACKCHAN_AUDIO_HOST", "127.0.0.1")
    port = int(os.environ.get("STACKCHAN_AUDIO_PORT", "5005"))
    ws_port = int(os.environ.get("RELAY_WS_PORT", "8771"))
    dest = (host, port)

    udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    log.info("relay: ws :%d -> udp %s:%d", ws_port, host, port)

    async def handler(ws):
        await _handle(ws, dest, udp)

    async with websockets.serve(handler, "127.0.0.1", ws_port, max_size=None):
        await asyncio.Future()  # run forever


if __name__ == "__main__":
    logging.basicConfig(
        level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s"
    )
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
