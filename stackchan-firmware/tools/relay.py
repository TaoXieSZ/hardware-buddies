#!/usr/bin/env python3
"""Mac-side serial<->gateway relay for the StackChan USB agent client.

Bridges the CoreS3 device (over USB serial) to the local voice gateway, so the
device needs no WiFi — it works on any locked-down corporate network as long as
it's plugged into this Mac.

    device --USB serial--> relay.py --HTTP--> localhost:60630 voice gateway --> agent

Wire protocol (115200 baud, newline-terminated):
    device -> here : "@ASK <prompt>"
    here -> device : "@REPLY <text>"   (newlines flattened to spaces)
                     "@ERR <message>"

The gateway bearer is read from dispatch/.env (VOICE_GATEWAY_TOKEN) so no secret
sits on the command line. Requires pyserial (use the PlatformIO python which has
it: ~/.platformio/penv/bin/python).

Usage: python3 relay.py [/dev/cu.usbmodemXXXX] [gateway_url]
"""
import json
import sys
import time
import urllib.request

import serial  # pyserial

PORT = sys.argv[1] if len(sys.argv) > 1 else "/dev/cu.usbmodem2101"
GATEWAY = sys.argv[2] if len(sys.argv) > 2 else "http://127.0.0.1:60630/v1/voice/chat/completions"
ENV = "/Users/txie/OpenSourceProjects/agent-farm/dispatch/.env"
DEFINITION = "stackchan-voice"

def load_token() -> str:
    for line in open(ENV):
        if line.startswith("VOICE_GATEWAY_TOKEN="):
            return line.split("=", 1)[1].strip()
    raise SystemExit("no VOICE_GATEWAY_TOKEN in dispatch/.env")

def ask_gateway(token: str, prompt: str) -> str:
    body = json.dumps({
        "model": DEFINITION,
        "stream": False,
        "messages": [{"role": "user", "content": prompt}],
    }).encode()
    req = urllib.request.Request(
        GATEWAY, data=body, method="POST",
        headers={"Content-Type": "application/json", "Authorization": f"Bearer {token}"},
    )
    with urllib.request.urlopen(req, timeout=90) as r:
        data = json.loads(r.read())
    return data.get("choices", [{}])[0].get("message", {}).get("content", "(空回复)")

def flatten(text: str) -> str:
    # The device shows the reply on one logical line; collapse newlines/tabs so
    # the "@REPLY <text>\n" framing stays intact.
    return " ".join(text.split())

def open_serial() -> "serial.Serial":
    """Open the port without asserting DTR/RTS (ESP32-S3 native USB resets on
    DTR). Reconnect callers handle the re-enumeration that a reset triggers."""
    ser = serial.Serial()
    ser.port = PORT
    ser.baudrate = 115200
    ser.timeout = 1
    ser.dtr = False
    ser.rts = False
    ser.open()
    return ser

def main() -> None:
    token = load_token()
    print(f"[relay] {PORT} <-> {GATEWAY}  (definition={DEFINITION})")
    print("[relay] waiting for @ASK lines from the device… (Ctrl-C to stop)")
    buf = b""
    ser = None
    while True:
        try:
            if ser is None:
                ser = open_serial()
                print("[relay] serial open")
            chunk = ser.read(256)
            if chunk:
                # Diagnostic: show raw device output so we can confirm the app's
                # Serial actually reaches this USB port.
                preview = chunk.decode("utf-8", "replace").replace("\n", "\\n")
                print(f"[relay] rx<{len(chunk)}>: {preview[:120]}")
                buf += chunk
                while b"\n" in buf:
                    raw, buf = buf.split(b"\n", 1)
                    line = raw.decode("utf-8", "replace").strip()
                    if not line.startswith("@ASK "):
                        continue  # boot logs / noise
                    prompt = line[len("@ASK "):].strip()
                    print(f"[relay] ASK: {prompt}")
                    try:
                        reply = flatten(ask_gateway(token, prompt))
                        print(f"[relay] REPLY: {reply[:80]}")
                        ser.write((f"@REPLY {reply}\n").encode("utf-8"))
                    except Exception as e:  # noqa: BLE001 — surface failure to the device
                        msg = flatten(str(e))[:120]
                        print(f"[relay] ERR: {msg}")
                        ser.write((f"@ERR {msg}\n").encode("utf-8"))
            else:
                time.sleep(0.02)
        except (OSError, serial.SerialException) as e:
            # Device reset / re-enumerated (DTR on open, or a manual reset).
            print(f"[relay] serial dropped ({e}); reconnecting in 2s…")
            try:
                if ser:
                    ser.close()
            except Exception:  # noqa: BLE001
                pass
            ser = None
            buf = b""
            time.sleep(2)

if __name__ == "__main__":
    main()
