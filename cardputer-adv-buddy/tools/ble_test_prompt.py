#!/usr/bin/env python3
"""不依赖 cc-bridge,直接给 Cardputer-claude-buddy 喂一条带审批 prompt 的状态,
验证固件的审批面板 + 键盘回送。需要先停掉 cc-bridge daemon 释放 BLE 链路。

跑法（用装了 bleak 的 venv）：
  ~/.cc-bridge/venv/bin/python3 tools/ble_test_prompt.py

流程：扫 "Claude-" 设备 → 连 debug 服务 → 订阅 TX notify → 写一条
{"waiting":1,"prompt":{...}} → 你在 Cardputer 上按 ok/esc/a → 打印收到的决定行。
"""
import asyncio
from bleak import BleakScanner, BleakClient

DBG_TX = "b0c2dbe6-cc03-4000-8000-00805f9b34fb"   # device → us (notify)
DBG_RX = "b0c2dbe6-cc02-4000-8000-00805f9b34fb"   # us → device (write)

STATE = (
    '{"total":1,"running":0,"waiting":1,'
    '"prompt":{"id":"test-001","tool":"Bash","hint":"terraform apply -auto-approve"}}\n'
)

async def main():
    print("scanning for Claude- ...")
    dev = None
    for d in await BleakScanner.discover(timeout=6.0):
        if d.name and d.name.startswith("Claude-"):
            dev = d; break
    if not dev:
        print("没找到 Claude- 设备（cc-bridge 是否还占着链路？先停掉它）"); return
    print("found", dev.name, dev.address)

    async with BleakClient(dev) as c:
        got = []
        def on_notify(_, data: bytearray):
            s = data.decode("utf-8", "replace")
            print("[device→] ", s.strip())
            got.append(s)
        await c.start_notify(DBG_TX, on_notify)
        print("→ 推送审批 prompt（Cardputer 屏上应弹: Bash / terraform apply…）")
        await c.write_gatt_char(DBG_RX, STATE.encode(), response=False)
        print("→ 现在在 Cardputer 上按 ok(approve) / esc(deny) / a(always)，等 20s …")
        for _ in range(40):
            await asyncio.sleep(0.5)
            if any('"decision"' in g for g in got):
                print("✅ 收到决定，审批回送闭环成立"); break
        await c.stop_notify(DBG_TX)

asyncio.run(main())
