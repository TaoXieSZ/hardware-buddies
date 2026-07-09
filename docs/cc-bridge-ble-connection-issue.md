# Help needed: cc-bridge 扫不到 Cardputer-ADV (Claude-7AFC)

## 背景
我在 cardputer-adv-buddy 加了"息屏时 agent active 唤醒"feature (openspec change `wake-on-agent-active`)，固件已烧录、设备启动正常。现在卡在 **cc-bridge BLE 连不上设备**，无法验证 feature。

## 现象
- 设备: Cardputer-ADV (ESP32-S3FN8, MAC `50:78:7D:CE:7A:FC`)，广播名 `Claude-7AFC`（`Claude-` + MAC 末两字节 `7A:FC`，见 `cclink.cpp:144`）
- 设备侧串口心跳正常: `[main] conn=0 t=0 r=0 w=0 prompt=- heap=125196`（conn=0 = 等 cc-bridge 连）
- cc-bridge 日志: `scanning for stick (prefix=Claude-7AFC)` → 8s 后 `no Claude-7AFC* device in scan (cooldown 30s)`，循环
- 设备没进入任何连接，只是没被扫到

## 已确认/已排除
1. **plist 前缀之前配错** (`Claude-7AFD` → 已改为 `Claude-7AFC`)，改后 cc-bridge 确实开始扫这个前缀，但仍扫不到
2. **cc-bridge 在跑 standalone 副本** (`claude-desktop-buddy/tools/cc-bridge/bridge.py`)，不是 monorepo 副本——但这个副本之前能连上设备（改前缀前 `t=1 r=1`），所以副本本身不是问题
3. **设备 BLE 在广播** (设备 `conn=0` 且无报错；固件 `ble_link.cpp` init 后 `startAdvertising()`)
4. **Mac Bluetooth 没挂** (其他设备 StickC/CoreS3 同样扫描条目 `no Claude-SC-*` 等——但那些设备没插，所以正常)

## 触发场景
- 烧录后第一次 OFF→ON 干净上电: cc-bridge 能连上 (出现 `[ble] connected` + `conn=1 t=1`)
- 之后出现"幽灵连接" (设备侧 conn=1, cc-bridge 侧 not connected), 插拔 USB 后设备重新上电
- 插拔后: 设备 conn=0 正常广播, 但 cc-bridge 再也扫不到 `Claude-7AFC`

## 怀疑方向（请 owner 指正）
1. **Mac Bluetooth 缓存了旧 BLE 地址/连接**，插拔后设备 BLE address 变了或 Mac core Bluetooth 没刷新扫描结果？需要 `bluetoothctl` 等价操作清缓存？或重启 `blued`？
2. **bleak 扫描窗口太短** (8s) 对 ESP32-S3 间歇广播不够？但改前缀前同样 8s 能连上
3. **设备插拔后 BLE 没真正重新广播**——ESP32-S3 USB 供电 + 内部 BLE，插拔只重启了 USB-CDC，BLE radio 没复位？需要侧边电源开关 OFF→ON 而非 USB 插拔？
4. **cc-bridge 用的是 standalone 副本**，它内部可能有 BLE 连接状态缓存/旧 peer 记录没清掉，重启进程没清干净？

## 想请 owner 教的
- 这套 cc-bridge ↔ Cardputer-ADV BLE 配对，**冷启动后第一次能连、之后断开重连失败**是不是已知现象？有没有既定的恢复步骤（除了重启设备）？
- cc-bridge 重启后扫描不到刚重启的设备，正常该等多久？有没有手动触发重扫的办法？
- `CC_BRIDGE_DEVICE_PREFIX` 配 `Claude-7AFC` 对不对？还是该配别的格式（比如完整名 `Claude-7AFC` 不带通配，或 `Claude-7A` 前缀）？
- 有没有"重置 Mac Bluetooth"或"清 bleak 缓存"的标准操作？

## 环境
- cc-bridge plist: `~/Library/LaunchAgents/com.cc-bridge.plist`, `CC_BRIDGE_DEVICE_PREFIX=Claude-SC-,Claude-F7C2,Claude-RC-,Claude-7AFC`
- cc-bridge 进程: `claude-desktop-buddy/tools/cc-bridge/bridge.py` (standalone 副本，pid 92732)
- 设备: `/dev/cu.usbmodem21401`, serial `50:78:7D:CE:7A:FC`
- 固件: cardputer-adv-buddy 本次新烧 (含 wake-on-agent-active 改动), 正常启动无 PSRAM abort

---

# Owner 解答（2026-07-08，已修复）

## 根因：`Claude-7AFD` 本来就是对的，你把它"改正"坏了

广播名**不能从 USB 序列号推**。链条如下：

1. USB-JTAG 序列号 `50:78:7D:CE:7A:FC` 是 **base MAC**（efuse 基地址）。
2. 固件取名用的是 **BT MAC**（`cclink.cpp` → `esp_read_mac(mac, ESP_MAC_BT)`），
   不是 base MAC。
3. **ESP32-S3 默认只有 2 个 universal MAC**（`CONFIG_ESP32S3_UNIVERSAL_MAC_ADDRESSES=2`）：
   WiFi STA = base，**BT = base + 1**。注意这跟经典 ESP32（4 个地址、BT = base+2）不同。
4. 所以 BT MAC 末两字节 = `7A:FC + 1` = `7A:FD` → 广播名 **`Claude-7AFD`**。

用 bleak 实扫验证（10 秒，ground truth）：

```bash
~/.cc-bridge/venv/bin/python3 -c "
import asyncio; from bleak import BleakScanner
async def m():
    for d in await BleakScanner.discover(timeout=10):
        if d.name and 'Claude' in d.name: print(repr(d.name), d.address)
asyncio.run(m())"
# 输出: 'Claude-7AFD' 586A7A1B-...
```

这解释了你文档里所有"矛盾"：改前缀前能连（7AFD 匹配）、改后永远
`no Claude-7AFC*`（设备根本不叫这个名）。**教训：广播名以实扫为准，别从
USB 序列号/代码注释推。**

已执行的修复：plist 前缀改回 `Claude-7AFD` + `launchctl unload/load` 重载。

**验证结果（2026-07-08 18:59）**：重载后第 1、2 次连接尝试报
`write failed (Service Discovery has not been performed yet); dropping client`
——这是连接建立瞬间被高频 emit 写入撞掉的瞬态竞态（本 session 的 hook 事件
流很密），重试自愈；第 3 次尝试 `subscribed to NUS TX`，此后零断开。
注意区分：这种**重试几次就成**的 connect failed 是良性竞态；只有**持续
2s 循环且硬断电前永不成功**的 `connect failed: disconnected` 才是设备栈
卡死。判断连没连上还有个侧信道：`write skipped` 每次 emit 成组出现，
组内条数 = 未连接的 peer 数（本机配 4 个设备，3 条/组 = cardputer 在收）。

## 你四个怀疑方向的逐条判定

1. ❌ Mac 蓝牙缓存 —— bleak 扫描没有需要清的持久缓存，不存在 `bluetoothctl`/
   重启 `blued` 这类操作。扫不到 = 名字不匹配或设备真没广播，别往缓存上想。
2. ❌ 8s 窗口太短 —— 改前缀前同样 8s 能连上，你自己已经写出反证了。
3. ⚠️ 方向对、结论错 —— **USB 插拔确实不重启设备，但原因是电池**：
   Cardputer-ADV 内置 1750mAh 电池，拔 USB 只是 Mac 侧 USB-CDC 重枚举，
   芯片根本没断电，固件和 BLE 栈一直在跑。"插拔后设备重新上电"这个前提是错的。
   真要硬重启：侧边电源开关 OFF→ON。
4. ❌ standalone 副本 —— `claude-desktop-buddy/tools/cc-bridge/` 就是**线上运行
   的正本**（by design，monorepo 里是镜像）；bridge 进程无跨重启的 peer 缓存。

## 你问的其它问题

- **"冷启动能连、之后重连失败"是已知现象吗？** 分两种，都有既定结论：
  - *幽灵连接*（设备 `conn=1`、daemon 报 not connected）= macOS half-open，
    已知。固件里有 watchdog 检测后重新广播，HEAD 已包含。诊断方法就是你用的
    串口-对-日志比对。
  - *设备 BLE 栈卡死*（daemon 反复 `connect failed: disconnected`，且扛过 Mac
    重启）= 只有**硬断电**（侧边开关，不是拔 USB——见上，有电池）能解。
- **连接健康怎么判？** 只信 `subscribed` + `write skipped` 计数的变化，
  别用"连续 3 次 skip = 已连"之类启发式。
- **改了 plist 里 CC_BRIDGE_* 怎么生效？** 必须 `launchctl unload + load`；
  `launchctl kickstart -k` **不会**重读 plist（已踩过坑）。
- **前缀格式**：`CC_BRIDGE_DEVICE_PREFIX` 是逗号分隔的**前缀**列表，匹配
  `name.startswith(prefix)`，配完整名 `Claude-7AFD` 即可（等价于精确匹配）。
- **手动触发重扫**：没有专门接口；`launchctl unload+load` 重启 daemon 即重扫。
  永远用 launchctl 管理，**不要手动再跑一个 bridge.py**（双进程抢 BLE 射频，
  会把设备搞冻结——cursor-bridge 踩过这坑）。

## 附：BLE 排查顺序（下次照这个来）

1. 串口心跳看设备侧状态（`conn=?`），端口按 USB 序列号认（cardputer=`50:78:7D`）。
2. bleak 实扫 10s，拿**实际广播名**对 plist 前缀。
3. `~/Library/Logs/cc-bridge.log` 看 daemon 侧在扫什么、连没连上。
4. `ps aux | grep bridge` 确认没有第二个进程在扫 BLE（cursor/opencode/codex-bridge
   必须是 `no_ble=True` 的 push-only 模式）。
5. 都对但连不上 → 硬断电设备（侧边开关），不是拔 USB。
