# StopWatch Walkie · 交接文档（2026-08-18）

> 给下一个接手的 agent（DeepSeek）。你不需要任何会话历史，这份文档 + 仓库就是全部上下文。
> 终极目标见 `../docs/stopwatch-walkie-talkie-design.md`（agent 编排层对话机，grilling 定稿）。
> 先读根 `AGENTS.md`（monorepo 血泪坑清单），再读本文件。

## 0. 最重要的一句话

**另一个 session 正在主仓库 `/Users/taoxie/hardware-buddies` 做 Tab5 联合开发（tab5-walkie-buddy）。
不要碰它的目录、不要动 `~/.platformio/platforms/espressif32`（pioarduino，无 .piopm）、
不要合它未提交的改动。** 我们的工作区是独立 worktree：

- worktree：`/Users/taoxie/hardware-buddies-walkie`，分支 `agent/stopwatch-orchestrator`（从 main 切出，已 ff 合回 main 一次）
- 子项目：`stopwatch-walkie/`（固件）+ `tools/walkie-bridge/`（Mac daemon）
- 根 `AGENTS.md` 的烧录纪律（认 MAC 不认端口、平台不混用）全部适用

## 1. 当前状态

- **M1（steer 老会话）真机 E2E 已通过**（2026-08-18）：按住 KEYA 说话 → DashScope ASR →
  别名路由 → 圆屏 proposal → KEYA 批准 → cmux 注入 Codex 终端并收到回话。
- **steer 目标用 Codex，不用 Claude/Kimi**（用户明确不要 claude；kimi 见 §5 限制）。
  牺牲品会话：cmux `workspace:6`（steer-codex，`/tmp/steer-codex`，带代理环境变量启动）。
- **M2 是下一步**：`kimi -p` 无头编排器（转写文本 + cc-bridge 快照 → LLM → 结构化调度 JSON
  → 复用现有 proposal 流），插在 ASR 之后、multi_agent.propose 之前。之后 M3 spawn、M4 运维。
- **有一批已修未提交的修复**（见 §4）：单测全绿（native 14 + pytest 68），固件已烧上表，
  但真机复测（自动 Completed + KEYB 退出）用户尚未回报，接手后先复测再提交。

## 2. 运行手册（全部实测可用）

```bash
# pio：主 penv 是 Python 3.14；本机 platformio 不在 PATH
PIO=~/.platformio/penv/bin/pio

# 编译+烧录（必须带 PLATFORMIO_PLATFORMS_DIR，原因见 §4.3）
mkdir -p /tmp/ps-platforms
ln -sfn ~/.platformio/platforms/espressif32@6.12.0 /tmp/ps-platforms/espressif32
ln -sfn ~/.platformio/platforms/native /tmp/ps-platforms/native
cd /Users/taoxie/hardware-buddies-walkie/stopwatch-walkie
PLATFORMIO_PLATFORMS_DIR=/tmp/ps-platforms $PIO run -e m5stack-stopwatch \
    -t upload --upload-port /dev/cu.usbmodem21101   # 端口先认 MAC！见 §3

# 测试
$PIO test -e native                                   # 固件状态机 14 例
cd tools/walkie-bridge && .venv/bin/python -m pytest tests/ -q   # bridge 68 例

# bridge 启动（控制模式；.env 含 DashScope key + control secret + 别名，gitignored）
cd /Users/taoxie/hardware-buddies-walkie/stopwatch-walkie/tools/walkie-bridge
set -a && . ./.env && set +a && .venv/bin/python bridge.py --host 0.0.0.0 --port 8765
# dashboard: http://127.0.0.1:8766/  （/api/status 看 pipeline 和快照）

# 串口监听（pio device monitor 在无 TTY 环境必崩，用 pyserial）
~/.platformio/penv/bin/python - <<'EOF'
import serial, time
s = None
while True:
    try:
        if s is None:
            s = serial.Serial('/dev/cu.usbmodem21101', 115200, timeout=1)
            s.reset_input_buffer()
        chunk = s.read(4096)
        if chunk:
            with open('/tmp/stopwatch_live.log','ab') as f: f.write(chunk)
    except Exception as e:
        with open('/tmp/stopwatch_live.log','ab') as f:
            f.write(f"\n[[drop: {e}]]\n".encode())
        try: s.close()
        except Exception: pass
        s = None; time.sleep(2)
EOF

# cc-bridge 控制面直查（unix socket）
python3 -c "
import socket; s=socket.socket(socket.AF_UNIX); s.connect('/tmp/cc-bridge.sock')
s.sendall(b'{\"action\":\"control.snapshot\"}\n'); print(s.makefile().readline())"
```

依赖服务：cc-bridge daemon（launchd 常驻，提供 `/tmp/cc-bridge.sock` 控制面）、
cmux ≥ 0.64.6（Automation socket 模式）。GitHub/git 推送走代理
`git -c http.proxy=http://127.0.0.1:7897 push`。

## 3. 设备识别（认 MAC，别认端口，端口会变）

| 设备 | USB serial | 当前端口 | 备注 |
|---|---|---|---|
| **StopWatch（我们的）** | `28:84:85:43:AE:38` | usbmodem21101 | 烧这个 |
| Tab5（别人的 session） | `80:F1:B2:…` | usbmodem21201 | **别碰** |
| StackChan | `44:1B:F6:…` | — | 别碰 |

`$PIO device list | grep SER=` 确认后再烧。烧录前停掉串口监听，烧完重启。
手表卡死时 USB CDC 会整个消失，只能物理关机再开。

## 4. 已修未提交的修复（工作区脏文件，先提交）

1. **路由标点边界**（已提交 `56fea72`，main 已含）：中文 ASR 转写用全角「，。」，
   别名后等 ASCII 空格永远匹配不上 → `target_required`。`multi_agent.py` 加了
   `_match_prefix` 边界匹配。谐音别名兜底：`测试会话/测试绘画/测试对话/测试画画` → codex。
2. **卡 Running 双 bug**（未提交，`include/audio_loop.h` + `tools/walkie-bridge/bridge.py`）：
   - 固件：`onKeyBCancel` 在 Running 态返回 None（没出口），且重连后 `task_id` 还在会被
     拖回 Running。修复：Running 下 KEYB = 停止关注（clearControlDisplay → Ready，
     迟到终态事件被忽略；cc-bridge 没有杀任务能力，agent 的活继续在终端跑）。
   - bridge：`_observe_task` 只在 active→idle 才判完成，但 **Codex 面板在控制面永远报
     idle**（生命周期没接入），任务泄漏。修复：idle + 8s 安置窗口（`task_settle_seconds`）
     也判完成。启发式，对长任务会提前 Completed，可接受。
3. **pioarduino 平台遮蔽**（未提交，`platformio.ini` 改 `platformio/espressif32@6.12.0`）：
   Tab5 session 装的 pioarduino 55.03.35（`~/.platformio/platforms/espressif32`，无
   `.piopm`）无视版本钉遮蔽官方平台，且拒绝 Python 3.14 → 必须用
   `PLATFORMIO_PLATFORMS_DIR=/tmp/ps-platforms` 指向只含官方平台的目录。**这个坑还没记进
   根 AGENTS.md，提交时一起补上。**

## 5. 已知限制 / 待办

- **Kimi 不可 steer**：cmux 0.64.20 不给 kimi 面板写 `terminal.agent` 元数据，
  `control.snapshot` 看不到它。修法：cc-bridge 控制面按 hook 事件（cwd/title）反配面板
  （claude-code-buddy 仓库，OpenSpec 管辖，且和 Tab5 session 同仓库，动手前先打招呼）。
- Codex `permission_reply=false`：审批回路（WaitingPermission 震动、KEYA批/KEYB拒）
  代码在但没真机测过。
- 会话的 steer 前提：cmux 工作区启动 + 发过首个 prompt（生命周期 running 过）+
  cmux session JSON 有 agent 元数据。Claude/Codex 自动，OpenCode 有 fallback，Kimi 没有。
- ASR 谐音误识别会持续出现，加别名到 `.env` 的 `WALKIE_CONTROL_ALIASES_JSON`
  （**JSON 必须单引号包**，双引号会被 bash source 吃掉）。
- WiFi：SSID「团团最帅」/ 19951029，Mac IP 192.168.3.14（写死在固件 `include/walkie_config.h`，
  gitignored）。

## 6. M2 提示（kimi -p 编排器）

- `kimi` CLI 在 `/Users/taoxie/.kimi-code/bin/kimi`，无头模式 `kimi -p "<prompt>"`。
- 插入点：`bridge.py` ASR 完成后、路由前；输入 = 转写文本 + `control.snapshot` 的会话列表 +
  别名表；输出 = 结构化 JSON（agent/label/command 或 spawn 请求），校验后喂给现有
  `MultiAgentRouter`/proposal 流（圆屏确认依然是安全闸，不要绕过）。
- 固件协议 v2 不用动；这只是 bridge 内部策略层升级。
