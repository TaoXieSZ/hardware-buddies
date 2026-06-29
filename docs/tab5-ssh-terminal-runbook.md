# Runbook — Tab5 当独立 SSH 终端跑 Claude Code

把 M5Stack Tab5 刷成一台**独立的 SSH 终端**:自己拨 Wi-Fi、直接 `ssh` 到 Mac、`tmux attach`、
跑**已认证的** Claude Code。不依赖 cc-bridge / BLE / 任何 Mac daemon —— 这不是 buddy,是一台口袋终端。

> 首次跑通日期 2026-06-29。本 runbook 是当时实操的复现步骤 + 踩过的坑。
> 配套背景见 monorepo 的 `claude-code-buddy/docs/tab5-buddy-dev.md`(那是 buddy 固件,不同目标)。

---

## 0. 这是什么 / 前提

- **硬件**:M5Stack Tab5(ESP32-P4 主控 + **ESP32-C6-MINI-1U** 协处理器提供 Wi-Fi 6;P4 通过
  SDIO/ESP-Hosted 指挥 C6 上网)+ Tab5 键盘 + USB-C 数据线。
- **上游固件**:[`airpocket-soundman/Tab5_SSH_Client`](https://github.com/airpocket-soundman/Tab5_SSH_Client)
  (MIT,PlatformIO/Arduino;`ewpa/LibSSH-ESP32` + 自写 ANSI/VT 终端仿真 + 内嵌 MicroPython)。
  **直接采用,不自研。** clone 到 `/Users/txie/OpenSourceProjects/Tab5_SSH_Client`。
- **Mac**:macOS,装了 PlatformIO、tmux、(可选)`blueutil`。已在本机认证过 Claude Code。

---

## 1. 取固件 + 工具链版本

```bash
cd /Users/txie/OpenSourceProjects
git clone --depth 1 https://github.com/airpocket-soundman/Tab5_SSH_Client.git

# pioarduino 平台需要 PlatformIO Core ≥ 6.1.19;官方 stable 才 6.1.18 → 用 dev 渠道
pio upgrade --dev          # 升到 6.x dev(满足版本门槛)
```

## 2. 构建 —— ⚠️ 必须绕开 GitHub 限速(本 runbook 最关键的坑)

首次构建要从 GitHub 下 ~500MB 工具链/框架。**国内网络直连 GitHub release ~17KB/s,VPN 也救不了**,
会卡死(`Downloading 0% 10%...` 然后 0KB/s,进程 0% CPU,日志冻结,极易误判为"编译卡死")。

**解药:用 Espressif 国内镜像喂 idf_tools 的工具链下载**(实测 6.4MB/s):

```bash
cd /Users/txie/OpenSourceProjects/Tab5_SSH_Client
IDF_GITHUB_ASSETS=dl.espressif.cn/github_assets pio run -e tab5
```

- 这个 env 让 `idf_tools`/`esp_install` 从 `dl.espressif.cn` 拉真实工具链二进制(riscv32-esp-elf 等),
  不走限速的 github.com。
- pio Tool Manager 自己下的 arduino core/libs(走 pio 缓存 `~/.platformio/.cache/downloads/`,
  文件名 = `sha1(url+"")`)如果也慢,可用 `https://gh-proxy.com/<github-url>` 镜像 curl 下好按该 sha1
  塞进缓存。但上面那个 env 法已覆盖最大的卡点,优先用。
- 成功:`[SUCCESS] Took ~170s`,产出 `.pio/build/tab5/firmware.bin`(~3.2MB)。

> 诊断"卡住了吗":`lsof -p <pid> -a -i` 看是不是卡在 github 连接;`find ~/.platformio/dist -name '*.tmp'`
> 看工具链临时文件涨不涨。0% CPU + .tmp 不涨 = 下载 stall,不是编译卡死,别瞎杀进程。

## 3. 烧固件

```bash
# Tab5 用 USB-C 数据线插电脑,确认端口(P4 原生 USB-CDC,通常 /dev/cu.usbmodem1401)
ls /dev/cu.usbmodem*

pio run -e tab5 -t upload --upload-port /dev/cu.usbmodem1401
# 成功:Hash of data verified → Hard resetting → [SUCCESS]
```

设备重启后应显示 `Tab5 Terminal` 界面 + `Show kbd`/`Menu` 按钮 + 右侧状态栏。
字若乱码(豆腐块)才需要烧 LittleFS 字体(见第 4 步,uploadfs 会带上 `data/` 的字体)。

## 4. 配 Wi-Fi + SSH profile(免得在 Tab5 上手输隐藏密码)

固件**只支持密码认证**(`SshClient.cpp` 的 `ssh_userauth_password`,无 key auth)。把 Wi-Fi + SSH
写进 `data/profiles.local.json`(已被 `.gitignore`,密码不进版本库),烧进 LittleFS。

`data/profiles.local.json` 内容(占位符换成真值):
```json
{
  "wifi": [{ "name": "home", "ssid": "<你的WiFi名>", "password": "<WiFi密码>" }],
  "ssh":  [{ "name": "mac", "host": "<Mac局域网IP>", "port": 22, "user": "<Mac用户名>",
             "password": "<Mac登录密码>", "terminal": "xterm-256color" }],
  "keyboard": { "layout": "us", "terminalFont": "mono12", "terminalLineStep": 15, "swapCtrlCaps": false },
  "system": { "deviceName": "tab5", "region": "Asia/Shanghai", "utcOffsetMinutes": 480, "ntpServer": "ntp.aliyun.com" }
}
```

⚠️ **两个坑:**
1. **TextEdit(`open -e`)会把直引号 `"` 自动改成弯引号 `“ ”`** → JSON 解析失败、设备读不到 profile。
   写入后务必校验+换回直引号:
   ```bash
   python3 - <<'PY'
   import json
   p="data/profiles.local.json"; raw=open(p,encoding="utf-8").read()
   fixed=raw.replace("“",'"').replace("”",'"').replace("‘","'").replace("’","'")
   open(p,"w",encoding="utf-8").write(fixed); json.loads(fixed); print("JSON OK, smart-quotes fixed:", fixed!=raw)
   PY
   ```
2. **uploadfs 整体替换设备上的 profiles** → Wi-Fi 也必须填全,否则会覆盖掉设备当前能用的 Wi-Fi。

烧 LittleFS(复刻 `tools/flash_tab5.ps1`:临时把 local 拷成 `profiles.json` → uploadfs → 恢复公开样板,
避免密码留在被 git 跟踪的 `data/profiles.json`):
```bash
cd /Users/txie/OpenSourceProjects/Tab5_SSH_Client
cp data/profiles.json /tmp/tab5_profiles_pub_backup.json
cp data/profiles.local.json data/profiles.json
IDF_GITHUB_ASSETS=dl.espressif.cn/github_assets pio run -e tab5 -t uploadfs --upload-port /dev/cu.usbmodem1401
cp /tmp/tab5_profiles_pub_backup.json data/profiles.json   # 恢复,别让密码进 git
```

> ⚠️ 安全:密码**明文存 LittleFS**,dump flash 可见。在意就建专用受限 Mac 账号给 Tab5,或给固件加 key 认证。

## 5. Mac 侧:开 Remote Login + 取登录信息

```bash
whoami                       # SSH 用户名(本机=txie)
ipconfig getifaddr en0       # 局域网 IP,用 en0 的(别用 VPN 虚拟网卡 IP)
```

开 **Remote Login**:System Settings → General → Sharing → Remote Login 打开(允许 All users 或含该用户)。
验证(`Permission denied` = sshd 在跑且可达 = 正常;`Connection refused` = 没开):
```bash
ssh -o BatchMode=yes -o ConnectTimeout=5 <user>@<Mac-IP> "echo ok" 2>&1   # 期望 Permission denied(=可达)
```

## 6. 复用 cmux 的 Claude 认证 + 持久会话

- **cmux 没法 SSH attach**:cmux 是 GUI app,它的 pane 是 app 窗口里的终端,进不去那个具体会话。
- **但认证是按用户共享的**:Claude Code 认证存 macOS keychain(service `Claude Code-credentials`,
  acct=你的用户名),GUI 登录解锁后**同用户 SSH 会话也能读** → Tab5 上跑 `claude` 就是已登录的,
  等于复用 cmux 的认证。(自检:`echo hi | claude -p "reply: AUTHED" --max-turns 1` 返回 AUTHED 即认证可用。)
- **持久会话用 tmux**(断网重连不丢):Mac 上先建一个 detached 会话:
  ```bash
  tmux new-session -d -s tab5
  ```

## 7. 日常用法(Tab5 上)

```
ssh connect 0          # 连第 0 个存好的 SSH profile(密码内置,无需手输)
tmux attach -t tab5    # 接入持久会话
claude                 # 启动已认证的 Claude Code
```
合盖/断网后再 `ssh connect 0` → `tmux attach -t tab5`,会话还在。
若 SSH 会话偶尔读不到 keychain 提示 login → 在该会话里 `claude setup-token` 配长效 token。

---

## 排错速查

| 症状 | 原因 / 处理 |
|---|---|
| 构建下载卡 0%/极慢 | GitHub 限速 → 加 `IDF_GITHUB_ASSETS=dl.espressif.cn/github_assets` 重跑(已缓存的包会跳过) |
| `IncompatiblePlatform: 需要 ≥6.1.19` | `pio upgrade --dev` |
| 设备读不到 profile / Wi-Fi 配了不连 | profiles.local.json 被 TextEdit 弯引号污染 → 脚本换直引号 + `json.loads` 校验 |
| uploadfs 后设备 Wi-Fi 丢了 | profiles 里 Wi-Fi 没填全(uploadfs 整体替换),补全重烧 |
| SSH `Connection refused` | Mac Remote Login 没开 |
| SSH `password 错` | profile 里 Mac 密码不对,或弯引号污染;别用 VPN 网卡 IP,用 en0 |
| `claude` 提示要 login | SSH 会话读不到 keychain → `claude setup-token` |
| Tab5 没认到 USB 口 | 用**数据线**(非充电线);P4 原生 USB-CDC 是 `/dev/cu.usbmodem*` |

## 烧回 buddy 固件

这台 Tab5 现在跑的是 SSH Client。要换回 monorepo 的 Tab5 buddy(dashboard):
```bash
cd /Users/txie/OpenSourceProjects/hardware-buddies/claude-code-buddy
pio run -e m5stack-tab5 -t upload --upload-port /dev/cu.usbmodemNN
```

## 参考

- 上游:https://github.com/airpocket-soundman/Tab5_SSH_Client (MIT)
- SSH 库:https://github.com/ewpa/LibSSH-ESP32
- Tab5 硬件:https://docs.m5stack.com/en/core/Tab5
- buddy 固件(不同目标):`claude-code-buddy/docs/tab5-buddy-dev.md`
