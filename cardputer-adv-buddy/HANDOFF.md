# cardputer-claude-buddy — 接手文档（怎么配置 / 怎么继续）

给下一个会话（含在 Cursor 里继续）的操作手册。架构看 `README.md`,规划看 `openspec/changes/cardputer-claude-buddy/`。本文件专讲**配置、烧录、测试、当前状态**。

---

## ⚠️ 当前状态（接手第一眼看这里）

| 项 | 状态 |
|---|---|
| 固件功能 | clawd 随真实状态 / 会话计数角标 / 会话列表 / 审批面板+回送 —— **全部真机验证通过** ✅ |
| 内存(512KB 无 PSRAM) | 跑满时空闲 **142KB**,余量充裕 |
| 固件(deny-fix) | **已编译,未烧录** ← 下一步烧录(`.pio/build/cardputer-adv/firmware.bin`) |
| cc-bridge daemon | **被 kill 了**(为 BLE 直测释放链路),正常用要重启(见下) |
| OpenSpec change | `cardputer-claude-buddy` 21/21 任务完成 |

**接手第一步**:把 deny-fix 版 app 烧进去(`.pio/build/cardputer-adv/firmware.bin` 已 build 好)。原因:修了快速点击 deny 键漏检 bug——原来同时要求 `isChange() && isPressed()`,快速点击时 release 帧 `isPressed()` 已为 false 导致整帧漏掉;改为只用 `isChange()`(release 帧 `ks.word` 为空,不会双触发)。

---

## 设备 & 链路

- **设备**:Cardputer-ADV,BLE 广播名 `Claude-<BT MAC 末2字节>`(本机 = `Claude-7AFD`)。
- **协议**:BLE NUS,cc-bridge 连**未加密 debug 服务**:
  - service `b0c2dbe6-cc01-4000-8000-00805f9b34fb`
  - RX(写) `…cc02`  TX(notify) `…cc03`
- 收：状态 JSON `{total,running,waiting,completed,msg,entries[],prompt{id,tool,hint}}`(行分隔,`{` 开头）。
- 回送：审批决定 `{"cmd":"permission","id":"<id>","decision":"once|deny|always"}`。
- 键盘：
  - 审批态：`ok`(enter)=approve once / `` ` `` 或 fn+esc 或 `n`=deny / `a`=always
  - 会话列表：`tab`=开关,`,`/`.` 滚动,`` ` ``/esc 返回
  - **nudge（NORMAL 态,v3）**：键盘映射经 `cmd:"key"` 把指令打进 Mac 聚焦的 Claude 终端 ——
    | 键 | 动作 | 键 | 动作 |
    |---|---|---|---|
    | `1` | continue | `2` | run the tests |
    | `3` | explain what you did | `4` | stop (escape) |
    | `5` | yes | `r` | try again |
    | `c` | commit the changes | `f` | fix this |
    | `v` | PTT（inject Space）| `h` | KEY MAP 说明 |
    发送后屏底闪 `sent: <label>`。零改 bridge(cmd=="key" → CGEvent 注入)。
    **`v` PTT 前提**：先在 Claude 终端运行 `/voice tap`，之后 `v` 每次注入 Space 切换录音。
    **`h` HELP 覆盖层**：显示完整键位说明，再按 `h`/`` ` ``/esc 关闭。
    **前提**：Claude 终端窗口在 Mac 上聚焦。

---

## 烧录（这块板子的坑！）

ESP32-S3 原生 USB-Serial-JTAG 在「跑着固件」时重进 bootloader 极不稳。**必须走 ROM 下载模式**：

1. 侧电源开关 **OFF** → **按住 G0** → 上电（拨 ON/插 USB）→ **松开 G0**（屏黑正常）
2. 找下载模式端口（口号每次会变）：
   ```bash
   for p in /dev/cu.usbmodem*; do ~/.platformio/penv/bin/esptool.py --port $p --before no_reset chip_id 2>&1 | grep -q 'ESP32-S3' && echo $p; done
   ```
3. **只改 app 时只烧 firmware**（4.9M clawd 文件系统不变,跳过省时间）：
   ```bash
   cd .pio/build/cardputer-adv
   ~/.platformio/penv/bin/esptool.py --chip esp32s3 --port <PORT> --before no_reset --after hard_reset \
     write_flash -z 0x0 bootloader.bin 0x8000 partitions.bin 0x10000 firmware.bin
   ```
   首装或换了 clawd 包时再加 `0x310000 littlefs.bin`（先 `pio run -t buildfs`）。
4. 干净上电（OFF→ON,**不按 G0**）。
5. `upload_speed=115200`（platformio.ini 已设）——跳过最易失败的波特率切换。**别调高**。

> 走 BLE 不碰串口,所以烧录只需对付上面的 JTAG 问题。但 `pio run -t upload`（auto-reset 路径）在这块基本必失败,一律用上面的下载模式 + esptool。

---

## cc-bridge daemon（macOS 侧,驱动设备的那个）

当前从 claude-desktop-buddy 的一个 worktree 跑（**注意不是本 monorepo**）：

- 路径：`/Users/txie/OpenSourceProjects/claude-desktop-buddy/.claude/worktrees/sticks3-buddy`
- **重启 cc-bridge（Claude）**：
  ```bash
  cd /Users/txie/OpenSourceProjects/claude-desktop-buddy/.claude/worktrees/sticks3-buddy
  CC_BRIDGE_DEVICE_PREFIX="Claude-" CC_BRIDGE_TAB5_SERIAL=/dev/cu.usbmodem21401 \
  CC_BRIDGE_PTT_MODE=hold CC_BRIDGE_PTT_KEYCODE=61 PYTHONPATH=tools \
  ~/.cc-bridge/venv/bin/python3 tools/cc-bridge/bridge.py
  ```
- cursor-bridge（Cursor）类似,`CURSOR_BRIDGE_DEVICE_PREFIX="Cursor-"` + `tools/cursor-bridge/bridge.py`（之前一直在跑,PID 不固定）。
- socket：`/tmp/cc-bridge.sock`。
- **坑**：`CC_BRIDGE_TAB5_SERIAL` 指向的 usbmodem 口会被 daemon 常开+自动重连抢占,挡住该口烧录。本设备走 BLE 不受影响,但若那个口正好是 Cardputer 的 bootloader 口,烧录前先停 daemon。

### 审批为什么有时不弹（hook 配置）

- `~/.claude/settings.json` 的 PreToolUse **已装** `hook_permission.py`（line 411,同步:推 prompt 给设备 + 等决定）。
- `CC_BRIDGE_PERMISSION_ECHO` 默认 `"1"`(开),**不用配**;设 `"0"` 才关。
- **bypass 会跳过**:`hook_permission.py` 见 `CLAUDE_BYPASS_PERMISSIONS in (1/true/yes)` 直接放行、不推 prompt。后台/auto/`--dangerously-skip-permissions` 会话都不会弹审批。要弹 → 用**普通交互式**会话。
- 同时还有 **AhaKey 拨杆 hook**(line 393)并行;它若 auto 会先放行,Cardputer 的决定可能不算数。干净测审批把 AhaKey 拨杆也拨 manual。

---

## 不靠 cc-bridge 直测固件（验证审批 UI）

`tools/ble_test_prompt.py` 绕开整个 bridge,直接 BLE 喂一条审批 prompt、收回决定。**需先停 cc-bridge 释放 BLE 链路**：

```bash
# 1. 停 cc-bridge（free BLE）：kill 掉那个 Claude- 的 bridge.py 进程
pkill -f 'cc-bridge/bridge.py'
# 2. 跑测试（venv 里有 bleak）
cd cardputer-adv-buddy
~/.cc-bridge/venv/bin/python3 tools/ble_test_prompt.py
# 3. Cardputer 弹 "Bash / terraform apply…" → 按 ok/esc/a → 脚本打印决定
```
已验证 ok→`{"decision":"once"}` ✅；esc-fix 烧录后该出 `"deny"`。

---

## TODO（接手可继续）

1. **烧 nudge v3 app**（上面烧录流程）+ 验证 r/c/f/v/h 键；esc-fix 已包含在内，同时验证 esc=deny。
2. **重启 cc-bridge** 恢复正常使用。
3. **Cursor 变体**:build_flags 加 `-DBUDDY_BRAND_PREFIX='"Cursor-"'` → 设备广播 `Cursor-XXXX`,cursor-bridge 连。可在 platformio.ini 复制一个 `[env:cardputer-adv-cursor]` extends + 覆盖 flag。
4. **会话操作**（MVP 只读）:切换/向某 session 发指令需 bridge `target` 路由。
5. **归档**:`cardputer-coding-pet` change 的 proposal/specs 还写 avatar、实际是 clawd,对齐后 `openspec archive`。
6. **提交**:整个 `cardputer-adv-buddy/` 至今**未 commit**。
7. Phase 3:ES8311 mic 听写 / IR / 更多体感。

---

## 关键学习（别重新踩）

- **esc 键 = backtick**:Cardputer keymap `{'`','~',KEY_ESCAPE}`,KEY_ESCAPE 在 fn 层;单按出 `` ` ``。
- **无 PSRAM 但够用**:BLE(Bluedroid)+clawd sprite(64KB)+JSON 共存,空闲 142KB。
- **avatar 弃用**:m5stack-avatar 默认脸 320×240,240×135 上尺寸难调 → 改 clawd GIF(固定像素居中)。
- **烧录铁律**:下载模式 + esptool + 115200,别用 `pio -t upload`。
- 所有 SDK/协议调用都对 upstream 逐字核对(BLE←ble_bridge.cpp、JSON←data.h、决定←main.cpp:1428、键盘←Keyboard.h)。源头出处写在各 .cpp 文件头。
