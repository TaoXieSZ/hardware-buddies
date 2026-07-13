# Tab5 × ESP-Claw Runbook

把 M5Stack Tab5 (ESP32-P4) 刷成 [espressif/esp-claw](https://github.com/espressif/esp-claw)
（"Chat Coding" 设备端 AI agent 框架，吉祥物是只小龙虾 🦞）的完整记录与复现步骤。

**状态**：2026-07-11 真机跑通。面板亮屏、触摸可用、Web 控制台可从局域网管理、DeepSeek 已接。
本项目**不是** buddy 家族成员（不走 cc-bridge/BLE），是把 Tab5 当独立 esp-claw 设备。它**覆盖**了之前
Tab5 上的 SSH 终端固件（见 `docs/tab5-ssh-terminal-runbook.md`；SSH 工程仍在，可随时烧回）。

---

## 0. 关键坐标（认口/认设备）

| 项 | 值 |
|---|---|
| 设备 | M5Stack Tab5，ESP32-P4 rev **v1.3** |
| **MAC（唯一可信标识）** | `80:f1:b2:d1:51:7d` |
| 串口 | `/dev/cu.usbmodem1401`（**端口号会变，只认 MAC**；`esptool.py read_mac` 核对） |
| 面板批次 | **2026-04+ 变体：ST7121 面板 + ST7123 触摸**（不是老批次 ILI9881C + GT911） |
| ESP-IDF | **v5.5.4**，装在 `~/esp/esp-idf-v5.5.4`（本机首个 IDF 环境） |
| esp-claw 源码 | `/Users/txie/OpenSourceProjects/esp-claw` |
| 用的板配置 | **`m5stack_tab5_st7121`**（自建变体，见 §3） |
| SoftAP（配网热点） | `esp-claw-214601`（无密码）@ `192.168.4.1` |
| STA IP（家里 Wi-Fi，DHCP） | `192.168.0.102`（当前值，允许变化） |
| LLM provider | DeepSeek |

---

## 1. 面板变体坑（本机为什么必须自建板配）

esp-claw 官方 `m5stack_tab5` 板配假设老批次硬件（ILI9881C 面板 + GT911 触摸）。本机是 2026-04 后
批次，硬件换成了 ST7121 面板 + ST7123 触摸 → 用官方板配的症状：

- **黑屏**（面板初始化序列不匹配）
- 串口日志 `E ... No LCD touch found on I2C bus: i2c_master`

同一块 ST7121 面板在 SSH 终端项目里也踩过（见记忆 `tab5-st7121-display-fixes`）。
Ground truth 是 M5Stack 官方 `m5stack/M5Tab5-UserDemo` 的 BSP
`bsp_display_new_with_handles_to_st7123()` —— 变体的所有参数逐字抄自它，不要凭记忆改。

---

## 2. 从零复现：环境 + 官方老板配（首刷）

```bash
# 2.1 装 ESP-IDF v5.5.4（走国内镜像提速）
git clone --branch v5.5.4 --depth 1 --recursive --shallow-submodules \
  https://github.com/espressif/esp-idf.git ~/esp/esp-idf-v5.5.4
export IDF_GITHUB_ASSETS="dl.espressif.cn/github_assets"
cd ~/esp/esp-idf-v5.5.4 && ./install.sh esp32p4

# ⚠️ 坑：install.sh 漏装 cmake/ninja（idf.py 会报 "cmake must be on PATH"）。手补：
source ~/esp/esp-idf-v5.5.4/export.sh
python3 ~/esp/esp-idf-v5.5.4/tools/idf_tools.py install cmake ninja

# 2.2 拿 esp-claw + bmgr 助手
git clone --depth 1 https://github.com/espressif/esp-claw.git ~/OpenSourceProjects/esp-claw
pip install esp-bmgr-assist        # 每个 IDF 环境装一次
```

> 每开新终端都要先 `source ~/esp/esp-idf-v5.5.4/export.sh` 再跑 `idf.py`。
> 环境变量 `IDF_GITHUB_ASSETS=dl.espressif.cn/github_assets` 在 install 和 build 时都加，避免下载卡死。

---

## 3. 自建 ST7121 板变体（本机实际用的）

变体已提交在 esp-claw 克隆里：
**commit `c182a77`，分支 `feat/m5stack-tab5-st7121-panel`（未 push）**。
目录：`~/OpenSourceProjects/esp-claw/application/edge_agent/boards/m5stack/m5stack_tab5_st7121/`

从官方 `m5stack_tab5` 复制后做的改动（全部对齐 M5Tab5-UserDemo BSP）：

| 文件 | 改动 |
|---|---|
| `setup_device.c` | panel 工厂 → `esp_lcd_new_panel_st7121`（`init_cmds=NULL`，用驱动内置序列）；touch 工厂 → `esp_lcd_touch_new_i2c_st7123` |
| `board_devices.yaml` | display chip `st7121`（依赖改为本地 vendored 组件）、`bits_per_pixel: 24`、`dpi_clock_freq_mhz: 70`、时序 h(back40/pulse2/front40) v(back24/pulse20/front200)；touch chip `st7123` @ `i2c_addr: 0xAA`(=0x55<<1)、`int_gpio_num: 23`；**删掉** GT911 时代的 `touch_power_ctrl` + `gpio_power_touch`（那是 GT911 地址选择 hack，ST7123 不需要，否则 GPIO23 与触摸 INT 冲突） |
| `board_peripherals.yaml` | DSI `lane_bit_rate_mbps: 1000 → 965` |
| `components/esp_lcd_st7121/` | 面板驱动，从 M5Tab5-UserDemo vendored（CMakeLists 去掉其私有 `package_manager` 引用，改纯 `idf_component_register`） |
| 删除 | 老板配的 `disp_init_data.h`（ILI9881C 专用初始化表，ST7121 用不上） |

touch 驱动 `espressif/esp_lcd_touch_st7123` 由组件管理器从仓库自动拉取（build 时联网）。

---

## 4. 编译 + 烧录

```bash
source ~/esp/esp-idf-v5.5.4/export.sh
export IDF_GITHUB_ASSETS="dl.espressif.cn/github_assets"
cd ~/OpenSourceProjects/esp-claw/application/edge_agent

# 选板（生成 components/gen_bmgr_codes/，set-target 不需要，bmgr 自动选 esp32p4）
idf.py bmgr -c ./boards -b m5stack_tab5_st7121
#   列全部板：idf.py bmgr -c ./boards -l

idf.py build

# 烧录前务必核对 MAC（端口会变，别盲烧）
esptool.py --port /dev/cu.usbmodem1401 read_mac      # 必须是 80:f1:b2:d1:51:7d
idf.py -p /dev/cu.usbmodem1401 flash
```

烧录写 7 个镜像（bootloader / partition-table / ota_data / edge_agent / emote_assets / system / storage），
每个 `Hash of data verified` = OK。

**回烧老批次 Tab5**：把 §4 里的板名换成官方 `m5stack_tab5` 即可，其余不变。

---

## 5. 首启验证（串口）

P4 是原生 USB-Serial-JTAG，被动读拿不到数据，要靠 DTR/RTS 软复位触发日志：

```python
import serial, time
s = serial.Serial("/dev/cu.usbmodem1401", 115200, timeout=1)
s.dtr=False; s.rts=True; time.sleep(0.1); s.rts=False   # 软复位
time.sleep(20); print(s.read(200000).decode(errors="replace"))
```

跑通的标志（真机实测日志）：

```
DEV_DISPLAY_LCD: Initializing LCD display: display_lcd, chip: st7121, sub_type: dsi
st7121: version: 1.0.0
ST7123: Firmware version: 1(1.80.1.16), Max.X: 720, Max.Y: 1280, Max.Touchs: 10
DEV_LCD_TOUCH: Successfully initialized LCD touch: lcd_touch, chip: st7123
Expression_load: Loading assets from partition: label=emote     ← 屏上出现小龙虾
wifi_manager: *** Provisioning AP active: esp-claw-214601 @ 192.168.4.1 ***
```

**良性告警**（老板配也有，不用管）：`swap_xy is not supported by this panel`；
未插 SD 卡时 `fs_sdcard ... sdmmc_init_ocr ... 0x107`。

---

## 6. 从 Mac 管理 + 写 API Key（日常入口）

设备和 Mac 同一 Wi-Fi 时，**Web 控制台就是管理入口**，不用碰串口：

```bash
# 设备 STA IP（会变时重新抓串口的 "sta ip: x.x.x.x" 行）
open "http://192.168.0.107/"
```

页面里配 DeepSeek：**Settings → LLM/模型配置 → Provider 选 DeepSeek**，填
- API Key：`sk-...`
- Base URL：`https://api.deepseek.com`
- Model：`deepseek-chat`（或 `deepseek-v4-pro`）

保存 → 点 **Web Chat** 发一句验证。配置存 NVS，重启不丢（`token=missing`/`model=(empty)`
日志会在保存后消失）。也可配 Wi-Fi / 时区 / Telegram / 飞书 / QQ / 搜索引擎(Tavily)。

**安全**：Web 控制台**局域网内无鉴权**，能下载设备上几乎所有内容（含刚填的 key）。别把 80 端口
暴露到公网；`esp-claw.local` 这台 mDNS 没解析成功，直接用 IP 最稳。

---

## 7. 横屏 Agent Farm 终端（本地 MacBook）

最终拓扑不依赖 Mac2。Tab5 连接当前 MacBook 上的独立
`tab5-local-gateway`；它复用 Agent Farm 的 `Dispatcher` 和本地
`agent-host:60620`，但**不启动**本地完整 dispatch，因此不会重复消费
Feishu、运行 cron/sweep 或 warm pool。

```text
Tab5 ESP-Claw
  ├─ lifecycle event <── HTTP bearer ── tab5-local-gateway
  └─ Web Chat ──────── OpenAI-compatible ──> tab5-operator
                                               │
                                               └─ local agent-host :60620
```

### 7.1 设备端

- 全局逻辑分辨率：1280×720；ST7121 物理 DSI 时序仍为 720×1280。
- USB 在左侧；PPA 统一旋转 emote/LVGL/Lua display。
- 默认背光 **40%**（LEDC duty 409/1023）。100% 会让 USB-C 电源区域严重发热；
  旧 SSH 固件 `M5.Display.setBrightness(100)` 是 0–255 量程，实际约 39%。
- `agentfarm_device_token` 写入 NVS；Web config 读取只返回空值 +
  `agentfarm_device_token_configured`，不会回显 secret。
- C 层 `require_bearer_setting("af_dev_token")` 在读取 body 前完成鉴权，
  token 不进入 Lua。
- `startup/boot_completed` 自动启动 display-exclusive、无 timeout 的
  `/system/.recovery/skills/agent_farm/scripts/agent_farm_terminal.lua`；
  Skill 同时恢复到 active DATA root，因此插 SD 卡时也不会因 `/fatfs` 未挂载而失效。
- 本板使用 lightweight memory；长期记忆由 `tab5-operator` 持有，设备不再并发
  发起 structured-memory auto extraction（否则主回复与后台请求会竞争同一 Agent）。
- UI：左侧宠物/连接状态，右侧 active + 8 条 recent history，底部只有本地动作；
  不提供虚假的 cancel/abort。

### 7.2 MacBook gateway

代码在 `/Users/txie/OpenSourceProjects/agent-farm/dispatch/`。PM2 进程名：
`tab5-local-gateway`。本地完整 `dispatch` 必须保持 stopped。

gateway 只加载 `config.tab5.yaml`（一个 local engine + `tab5-operator`）和
gitignored 的 `.env.tab5`，不读取包含 Feishu/其他 engine secret 的共享配置。
`.env.tab5` 需要这些键：

```text
TAB5_GATEWAY_TOKEN
TAB5_DEVICE_EVENT_TOKEN
TAB5_DEVICE_EVENT_URL=http://<TAB5_STA_IP>/api/lua/agentfarm/event
TAB5_EVENT_MODE=forward
TAB5_CHAT_ENABLED=true
TAB5_GATEWAY_HOST=0.0.0.0
TAB5_GATEWAY_PORT=60642
TAB5_DEFINITION_ID=tab5-operator
AGENT_HOST_API_SECRET_LOCAL
```

启动/停止：

```bash
cd /Users/txie/OpenSourceProjects/agent-farm/dispatch
npm run tab5-local-gateway:pm2:start
npm run tab5-local-gateway:pm2:restart
npm run tab5-local-gateway:pm2:stop
```

### 7.3 地址与 DHCP

- MacBook 当前 LAN IP：`192.168.0.106`
- Tab5 当前 STA IP：`192.168.0.102`（会漂移）
- Tab5 C6 Wi-Fi MAC：`58:e6:c5:21:46:00`
- P4 USB/JTAG MAC：`80:f1:b2:d1:51:7d`

本集成刻意保持现有 DHCP/Wi-Fi 配置，不要求修改路由器，也不占用 USB 作为
常驻数据通道。IP 漂移后从串口 `wifi --status` 读取 `sta_ip`，更新
`.env.tab5` 中的 `TAB5_DEVICE_EVENT_URL`，再只重启 `tab5-local-gateway`。
`esp-claw.local` 在本机仍无法解析。

### 7.4 已验证证据

- 40% 背光三分钟 A/B：USB-C 附近明显降温。
- 触摸：TL/TR/BL/BR/C、横向拖动、纵向坐标方向通过。
- 无关 `heap_probe.lua` 退出后 terminal 仍保持显示权；修复了跨 Lua state
  cleanup 错误释放 LVGL owner 的问题。
- 未授权 9KB body 在 C 层直接返回 401；正确 bearer 后 malformed/unsupported
  返回 400；重复事件 `changed=false`。
- gateway stop 超过 stale interval 后 UI 显示 OFFLINE 且 history 保留；恢复
  heartbeat 后自动 CONNECTED，不重播完成动画。
- `visual-check` running/success、persistent error 深红 ATTENTION 卡片均真机可见；
  raw error 不进入 UI。
- 首次真实请求固定到 `tab5-operator`，创建 Agent
  `agent-09905f84-b08b-4c64-bc12-2ebbd4bd7ecb`，用量归属正确；
  definition 保持 `mode: ask`、独立 memory、无 pool/trigger/MCP。
- 设备 `/api/webim/send` → local gateway → Agent → WebSocket 最终回复
  `WEBCHAT_OK`，完整 Web Chat 链路通过。

### 7.5 构建、刷机与回退

实现拆分：

- ESP-Claw 横屏/PPA/触摸/热管理：`33b5649`
- ESP-Claw Lua HTTP/display/stop runtime：`15dee4a`
- ESP-Claw Agent Farm terminal：`88ebc25`
- Agent Farm 独立 local gateway：`cf25270`

```bash
source ~/esp/esp-idf-v5.5.4/export.sh
cd ~/OpenSourceProjects/esp-claw/application/edge_agent
idf.py build
esptool.py --port /dev/cu.usbmodem1401 read_mac  # 必须 80:f1:b2:d1:51:7d
idf.py -p /dev/cu.usbmodem1401 flash
```

分层回退：停止 `tab5-local-gateway` → 设备显示 OFFLINE 并保留 history；
如需回到横屏前固件，回刷 ESP-Claw commit `c182a77`。

---

## 8. TODO / 后续

- [ ] `m5stack_tab5_st7121` 变体值得给上游 espressif/esp-claw 提 PR（官方目前只支持老批次 Tab5）。
      分支已在本地 `feat/m5stack-tab5-st7121-panel`，未 push。
- [ ] 若日后想回 SSH 终端固件：见 `docs/tab5-ssh-terminal-runbook.md`。
