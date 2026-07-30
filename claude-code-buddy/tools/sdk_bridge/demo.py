#!/usr/bin/env python3
"""
End-to-end demo: Anthropic SDK stream → StackChan BLE.

STOP THE CC-BRIDGE FIRST — only one process can hold the BLE connection:
    launchctl bootout gui/$(id -u)/com.cc-bridge

Usage:
  cd .../tools && \
  PYTHONPATH=.../tools \
  ~/.cc-bridge/venv/bin/python3 sdk_bridge/demo.py \
    --prompt "What is 15% of 340?"
"""

import argparse, asyncio, sys, threading
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import anthropic
from sdk_bridge import AgentState, BleWriter, DEVICE_PREFIX, feed_sdk_stream
from sdk_bridge.bridge import heartbeat_loop, ensure_ble_connected

MODEL = "claude-sonnet-4-20250514"


def _run_stream(args, state: AgentState, done_event: threading.Event):
    client = anthropic.Anthropic()
    try:
        with client.messages.stream(
            model=args.model,
            max_tokens=args.max_tokens,
            messages=[{"role": "user", "content": args.prompt}],
        ) as stream:
            feed_sdk_stream(stream, state)
    except Exception as e:
        state.on_error(str(e))
        print(f"API ERROR: {e}")
    finally:
        done_event.set()


async def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--prompt", default="Explain quantum computing in one sentence.")
    ap.add_argument("--max-tokens", type=int, default=1024)
    ap.add_argument("--model", default=MODEL)
    args = ap.parse_args()

    # ── BLE ──
    print(f"Scanning for {DEVICE_PREFIX}...")
    ble = BleWriter(device_prefix=DEVICE_PREFIX)
    state = AgentState()

    hb = asyncio.create_task(heartbeat_loop(ble, state))
    rc = asyncio.create_task(ensure_ble_connected(ble))

    print("Waiting for BLE...")
    for _ in range(15):
        if ble.any_connected:
            print("BLE connected.")
            break
        await asyncio.sleep(2)
    else:
        print("BLE not found. Is the cc-bridge still running? Stop it:")
        print("  launchctl bootout gui/$(id -u)/com.cc-bridge")
        print("Continuing without hardware.\n")

    # ── API ──
    print(f"Prompt: {args.prompt}\n")
    done = threading.Event()
    threading.Thread(target=_run_stream, args=(args, state, done), daemon=True).start()

    while not done.is_set():
        await asyncio.sleep(0.5)

    print(f"\nDone. {state.tokens_today} tokens.")
    print(f"Entry: {state.entries[0] if state.entries else '(none)'}")

    await asyncio.sleep(2)
    hb.cancel()
    rc.cancel()

if __name__ == "__main__":
    asyncio.run(main())
