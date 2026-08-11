# StopWatch Walkie Bridge

Prototype Mac-side bridge for the StopWatch push-to-talk audio loop.

## Setup

```bash
cd stopwatch-walkie/tools/walkie-bridge
python3 -m venv .venv
. .venv/bin/activate
pip install -r requirements-dev.txt
```

## Run

```bash
export DASHSCOPE_API_KEY=...
export DASHSCOPE_BASE_URL=https://YOUR_WORKSPACE_ID.cn-beijing.maas.aliyuncs.com/compatible-mode/v1
python bridge.py --host 0.0.0.0 --port 8765
```

The read-only local dashboard starts by default at
[http://127.0.0.1:8766/](http://127.0.0.1:8766/). Configure it with
`WALKIE_DASHBOARD_ENABLED` and `WALKIE_DASHBOARD_PORT`; its bind address is
intentionally fixed to IPv4 loopback. Port 8766 is independent of the watch
WebSocket on 8765 and the cc-bridge page on 18765.

`DASHSCOPE_BASE_URL` must point at the OpenAI-compatible Model Studio endpoint
for the chosen region/workspace. The API key is only read from the local
environment and is never sent to the device.

## Test

```bash
pytest
```

The live DashScope test is skipped unless all of these are present:

```bash
export DASHSCOPE_API_KEY=...
export DASHSCOPE_BASE_URL=...
export WALKIE_BRIDGE_LIVE_ASR=1
pytest -m live
```
