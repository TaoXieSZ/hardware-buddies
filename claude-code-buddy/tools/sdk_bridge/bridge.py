#!/usr/bin/env python3
"""
sdk-bridge — Anthropic Agent SDK → StackChan BLE companion.

Streams agent state from the Anthropic Python SDK to the M5Stack StackChan
over BLE. Maps SDK stream events to the heartbeat schema the firmware already
understands — zero firmware changes required.

IMPORTANT: The StackChan's debug BLE service can only be held by one process.
Stop the cc-bridge before using the SDK bridge:
  launchctl bootout gui/$(id -u)/com.cc-bridge

Usage:
  python3 tools/sdk_bridge/bridge.py
"""

import asyncio
import os
import pathlib
import sys
from dataclasses import dataclass, field

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
from buddy_core.core import BuddyState, BleWriter  # noqa: E402

DEVICE_PREFIX = "Claude-SC-"
HEARTBEAT_INTERVAL = 10.0


@dataclass
class AgentState(BuddyState):
    """Maps Anthropic SDK stream events to BuddyState for the StackChan.

    Deltas are accumulated into buffers and flushed as entries when
    a content_block_stop arrives. This avoids one-entry-per-character spam.
    """
    active_tool: str = ""
    current_model: str = ""
    _text_buf: str = ""
    _thinking_buf: str = ""

    def on_message_start(self, message) -> None:
        self.running = 1
        self.current_model = getattr(message, "model", "")
        self.total = max(1, self.total)
        self.msg = "thinking..."
        self._text_buf = ""
        self._thinking_buf = ""
        self.set_session_state("default", "thinking")

    def on_content_block_start(self, block) -> None:
        bt = getattr(block, "type", "unknown")
        self._text_buf = ""
        self._thinking_buf = ""

        if bt == "tool_use":
            self.active_tool = getattr(block, "name", "tool")
            self.msg = f"running: {self.active_tool}"
            self.set_session_state("default", "tool")
        elif bt in ("thinking", "text"):
            self.msg = "thinking..."
            self.set_session_state("default", "thinking")

    def on_content_block_delta(self, delta) -> None:
        dt = getattr(delta, "type", "unknown")

        if dt == "text_delta":
            self._text_buf += getattr(delta, "text", "")
        elif dt == "thinking":
            self._thinking_buf += getattr(delta, "thinking", "")

    def on_content_block_stop(self) -> None:
        # Flush accumulated text as a single entry
        if self._text_buf:
            text = self._text_buf.strip()
            if text:
                self.add_entry(text[:91])
            self._text_buf = ""

    def on_message_stop(self, usage_output: int = 0) -> None:
        self.running = 0
        self.tokens += usage_output
        self.tokens_today += usage_output
        self.completed = True
        self.msg = f"done ({usage_output} tokens)"
        self._text_buf = ""
        self._thinking_buf = ""
        self.set_session_state("default", "idle")

    def on_error(self, error_msg: str) -> None:
        self.running = 0
        self.msg = f"error: {error_msg[:60]}"
        self.add_entry(f"error: {error_msg[:80]}")
        self.set_session_state("default", "idle")


def feed_sdk_stream(stream, state: AgentState) -> None:
    """Feed a single SDK stream event into the agent state.

    Usage:
        with client.messages.stream(...) as stream:
            for event in stream:
                _dispatch(event, stream, state)
    """
    for event in stream:
        t = event.type
        if t == "message_start":
            state.on_message_start(event.message)
        elif t == "content_block_start":
            state.on_content_block_start(event.content_block)
        elif t == "content_block_delta":
            state.on_content_block_delta(event.delta)
        elif t == "content_block_stop":
            state.on_content_block_stop()
        elif t == "message_stop":
            snap = stream.current_message_snapshot
            usage = snap.usage.output_tokens if snap and snap.usage else 0
            state.on_message_stop(usage_output=usage)


async def heartbeat_loop(ble: BleWriter, state: AgentState):
    while True:
        try:
            await ble.write(state.to_payload())
        except Exception:
            pass
        await asyncio.sleep(HEARTBEAT_INTERVAL)


async def ensure_ble_connected(ble: BleWriter):
    while True:
        try:
            if not await ble.ensure_connected():
                await asyncio.sleep(30)
        except Exception:
            await asyncio.sleep(5)


async def main():
    ble = BleWriter(device_prefix=DEVICE_PREFIX)
    state = AgentState()

    hb = asyncio.create_task(heartbeat_loop(ble, state))
    rc = asyncio.create_task(ensure_ble_connected(ble))

    print(f"sdk-bridge ready. Scanning for {DEVICE_PREFIX}...")
    print("Use feed_sdk_stream(stream, state) to map SDK events to the StackChan.")

    await asyncio.gather(hb, rc)


if __name__ == "__main__":
    asyncio.run(main())
