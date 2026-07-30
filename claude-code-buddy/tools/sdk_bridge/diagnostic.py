#!/usr/bin/env python3
"""
Stream the Anthropic SDK and print every event.
Shows the raw event structure so you can see what to map to StackChan states.

Usage:
  cd .../tools && PYTHONPATH=.../tools ~/.cc-bridge/venv/bin/python3 sdk_bridge/diagnostic.py
"""

import sys, os, json
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import anthropic

MODEL = "claude-sonnet-4-20250514"

def _fmt(obj):
    if hasattr(obj, "model_dump"):
        return obj.model_dump()
    if hasattr(obj, "__dict__"):
        return str(obj)[:200]
    return str(obj)

def main():
    print(f"model: {MODEL}")
    print(f"prompt: Explain quantum computing in one sentence.\n")

    client = anthropic.Anthropic()

    try:
        with client.messages.stream(
            model=MODEL,
            max_tokens=256,
            messages=[{"role": "user", "content": "Explain quantum computing in one sentence."}],
        ) as stream:
            for event in stream:
                t = event.type
                print(f"[{t}]")

                if t == "message_start":
                    m = event.message
                    print(f"  model={m.model}  role={m.role}")
                    print(f"  usage.input_tokens={m.usage.input_tokens}")

                elif t == "content_block_start":
                    cb = event.content_block
                    print(f"  index={event.index}")
                    print(f"  type={cb.type}")
                    if cb.type == "text":
                        print(f"  text  (streaming deltas follow)")
                    elif cb.type == "tool_use":
                        print(f"  name={cb.name}  id={cb.id}")

                elif t == "content_block_delta":
                    delta = event.delta
                    print(f"  index={event.index}")
                    if delta.type == "text_delta":
                        print(f"  text_delta={repr(delta.text[:80])}")

                elif t == "content_block_stop":
                    print(f"  index={event.index}")

                elif t == "message_delta":
                    d = event.delta
                    print(f"  stop_reason={d.stop_reason}")
                    u = event.usage
                    if u:
                        print(f"  output_tokens={u.output_tokens}")

                elif t == "message_stop":
                    print("  DONE")
                    snap = stream.current_message_snapshot
                    print(f"  final output_tokens={snap.usage.output_tokens}")

                else:
                    print(f"  raw={_fmt(event)}")

        print("\nstream complete.")

    except anthropic.AuthenticationError:
        print("ERROR: ANTHROPIC_API_KEY not set. Set it and retry:")
        print("  export ANTHROPIC_API_KEY=sk-ant-...")

if __name__ == "__main__":
    main()
