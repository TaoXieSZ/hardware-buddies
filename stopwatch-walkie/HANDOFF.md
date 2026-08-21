# StopWatch Walkie · 交接文档（2026-08-21）

> 给下一个接手的 agent。你不需要任何会话历史,这份文档 + 仓库就是全部上下文。
> 终极目标见 `../docs/stopwatch-walkie-talkie-design.md`;编排层插件设计见
> `../docs/plans/2026-08-20-walkie-dsh-plugin-design.md` 与实现计划
> `../docs/plans/2026-08-20-walkie-dsh-plugin-plan.md`。
> 先读根 `AGENTS.md`(monorepo 血泪坑清单),再读本文件。

## 0. 最重要的一句话

**另一个 session 正在主仓库 `/Users/taoxie/hardware-buddies` 做 Tab5 联合开发
(tab5-walkie-buddy + walkie screen 角色)。不要碰它的目录、不要动
`~/.platformio/platforms/espressif32`、不要合它未提交/暂存的改动**——main 的
暂存区里有它对 walkie 修复的回退 + ScreenHub screen 角色工作,一根指头都别碰。
我们的工作区是独立 worktree:

- worktree:`/Users/taoxie/hardware-buddies-walkie`,分支 `agent/stopwatch-orchestrator`
- 子项目:`stopwatch-walkie/`(固件)+ `tools/walkie-bridge/`(Mac daemon)+
  `tools/walkie-dsh-plugin/`(DSH 插件)

## 1. 当前状态(2026-08-21)

- **M1 steer 真机 E2E 已过**(2026-08-18):PTT → DashScope ASR → 路由 → 圆屏提案
  → KEYA → cmux 注入 Codex 终端并收到回话。
- **编排层已升级为 DSH 插件(本分支 8 个新提交,`016ae7c`…`97f7a25`,均未 push)**:
  - bridge 新增回环 **brain API**(127.0.0.1:8767,Bearer token):status/events/
    队列长轮询/decision/proposals/tts。单测 86 passed + 1 skipped。
  - **大脑优先路由**:ASR 后转写入队,brain 15s 超时兑底确定性 `MultiAgentRouter`;
    decision 目标仍过唯一匹配校验;白名单正则命中(默认查询类)直接注入
    (dashboard 事件 `direct=true`),否则圆屏提案。固件零改动。
  - 插件 `walkie-dsh-plugin/`(node 12 例纯函数测试):6 个工具 + 专职 headless
    值班会话(`walkie-duty`)路由循环,硬化 prompt(转写标注为不受信任数据)。
- **P3 联调进行中**:bridge 已带 brain 配置重启并验证(8765/8766/8767 全 listen,
  watch 已重连认证);插件已注册进 `~/.dsh/profiles/web/cordis.patch.yml`(insert
  id `dsh-walkie`);**用户重启 dsh web 后插件才加载**——这一步是外部依赖,
  重启后按 §4 清单做真机 E2E。

## 2. 运行手册(全部实测可用)

```bash
# pio:主 penv 是 Python 3.14;本机 platformio 不在 PATH
PIO=~/.platformio/penv/bin/pio

# 编译+烧录(必须带 PLATFORMIO_PLATFORMS_DIR,原因见 §5.3)
mkdir -p /tmp/ps-platforms
ln -sfn ~/.platformio/platforms/espressif32@6.12.0 /tmp/ps-platforms/espressif32
ln -sfn ~/.platformio/platforms/native /tmp/ps-platforms/native
cd /Users/taoxie/hardware-buddies-walkie/stopwatch-walkie
PLATFORMIO_PLATFORMS_DIR=/tmp/ps-platforms $PIO run -e m5stack-stopwatch \
    -t upload --upload-port /dev/cu.usbmodem21101   # 端口先认 MAC!见 §3

# 测试
cd tools/walkie-bridge && .venv/bin/python -m pytest tests/ -q   # 86 passed, 1 skipped
cd ../walkie-dsh-plugin && node --test                           # 12 passed

# bridge 启动(控制 + brain 模式;.env 含 DashScope key + control secret +
# brain token + 白名单,gitignored)
cd /Users/taoxie/hardware-buddies-walkie/stopwatch-walkie/tools/walkie-bridge
set -a && . ./.env && set +a && .venv/bin/python bridge.py --host 0.0.0.0 --port 8765
# dashboard: http://127.0.0.1:8766/  brain API: http://127.0.0.1:8767/api/v1/status

# brain API 冒烟(token 在 .env 的 WALKIE_BRAIN_TOKEN)
curl -s -H "Authorization: Bearer $TOKEN" http://127.0.0.1:8767/api/v1/status

# 串口监听(pio device monitor 在无 TTY 环境必崩,用 pyserial)
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

# cc-bridge 控制面直查(unix socket)
python3 -c "
import socket; s=socket.socket(socket.AF_UNIX); s.connect('/tmp/cc-bridge.sock')
s.sendall(b'{\"action\":\"control.snapshot\"}\n'); print(s.makefile().readline())"
```

依赖服务:cc-bridge daemon(launchd 常驻,`/tmp/cc-bridge.sock` 控制面)、
cmux ≥ 0.64.6(Automation socket 模式)、dsh web host(插件宿主)。GitHub/git 推送
走代理 `git -c http.proxy=http://127.0.0.1:7897 push`。

## 3. 设备识别(认 MAC,别认端口,端口会变)

| 设备 | USB serial | 当前端口 | 备注 |
|---|---|---|---|
| **StopWatch(我们的)** | `28:84:85:43:AE:38` | usbmodem21101 | 烧这个 |
| Tab5(别人的 session) | `80:F1:B2:…` | usbmodem21201 | **别碰** |
| StackChan | `44:1B:F6:…` | — | 别碰 |

`$PIO device list | grep SER=` 确认后再烧。烧录前停掉串口监听,烧完重启。
手表卡死时 USB CDC 会整个消失,只能物理关机再开。

## 4. dsh web 重启后的真机 E2E 清单(2026-08-21 更新)

插件已随 dsh web 重启加载(2026-08-21 09:51,host PID 以 `lsof -iTCP:3080` 为准);
bridge 以 §2 命令常驻。**合成语音假 watch 客户端已验证全链路**
(`/tmp/walkie_fake_watch.py`):真实 DashScope ASR → duty 会话 LLM 决策(2.5s)→
`brain.routed` → 白名单直发(`direct=true`,cmux 注入 steer-codex 并观察到 agent
活动)/ 非白名单圆屏提案。剩真表按键项:

| # | 动作 | 预期 | 状态 |
|---|---|---|---|
| 1 | 任意 DSH 会话用 `walkie_status`;duty 会话懒创建 | 工具返回快照;首条语音后出现 `walkie-duty` 会话 | ✅ 已验证 |
| 2 | 按住 KEYA 说「codex 帮我跑一下测试」 | 大脑路由 → 圆屏提案 → KEYA → cmux 注入 | ✅ 真表通过(proposal.approved → task.accepted → task.completed) |
| 3 | 按住 KEYA 说「codex git status」 | 白名单命中,**无圆屏提案**,直接注入 | ✅ 直发链路已由假 watch 闭环验证(direct=true → 注入);真表未复测(同链路) |
| 4 | 任意提案按 KEYB | 拒绝,不注入 | ✅ 真表通过(proposal.rejected,无 dispatch) |
| 5 | DSH 会话里 `walkie_propose {text:"…", agent:"codex"}` | 圆屏出现卡片,KEYA/KEYB 决定 | ✅ 已推真表(gated=true,卡片 60s 过期) |
| 6 | `walkie_say "你好"` | 手表 TTS 播报 | ✅ 已验证(tts.completed 145KB) |
| 7 | 临时注释 cordis.patch.yml 的 dsh-walkie 条目并重启 | 语音走路由器兑底,M1 行为不变 | ✅ brain 关闭模式已 live 验证(路由器提案;含 CJK 边界修复) |
| 8 | bridge 宕机恢复 / duty 会话重建 | 循环退避后自动恢复长轮询;会话复用 | ✅ 宕机恢复已验证(35s 内重连;会话未重建而是复用) |

## 5. 已知限制 / 待办 / 坑

1. **Kimi 不可 steer**:cmux 0.64.20 不给 kimi 面板写 `terminal.agent` 元数据,
   `control.snapshot` 看不到它。修法:cc-bridge 控制面按 hook 事件反配面板
   (claude-code-buddy 仓库,OpenSpec 管辖,且和 Tab5 session 同仓库,动手前先打招呼)。
2. Codex `permission_reply=false`:审批回路代码在但没真机测过;brain 仲裁
   (permission 工作项 + `walkie_resolve`)同样未真机验证。
3. **pioarduino 平台遮蔽**:Tab5 session 装的 pioarduino 55.03.35
   (`~/.platformio/platforms/espressif32`,无 `.piopm`)无视版本钉遮蔽官方平台,
   且拒绝 Python 3.14 → 必须用 `PLATFORMIO_PLATFORMS_DIR=/tmp/ps-platforms`。
   这个坑还没记进根 AGENTS.md,提交时一起补上。
4. 值班会话的模型/预设默认跟 profile,烧钱快(每句语音一次 LLM);想省钱后续给
   duty 换便宜模型。大脑 15s 超时(bridge 侧 `WALKIE_BRAIN_TIMEOUT`)。
5. 白名单默认只含查询类(`^git\s+(status|diff|log)\b`、`^(查看|看看|查一下)`、
   `^tail\s`、`^cat\s`);改 `WALKIE_BRAIN_WHITELIST_JSON`(JSON 数组,单引号包)。
   白名单匹配的是大脑回执的 command 文本,不是 ASR 转写原文。
6. ASR 谐音误识别会持续出现,加别名到 `.env` 的 `WALKIE_CONTROL_ALIASES_JSON`
   (**JSON 必须单引号包**,双引号会被 bash source 吃掉)。
7. WiFi:SSID「团团最帅」/ 19951029,Mac IP 192.168.3.14(写死在固件
   `include/walkie_config.h`,gitignored)。
8. brain API 与 dashboard 都是 ThreadingHTTPServer;brain 的 HTTP 线程经
   `run_coroutine_threadsafe` 挂回事件循环(超时 30s),测试里 HTTP 调用必须放
   worker 线程,否则死锁(见 `tests/test_brain_api.py` 的 to_thread 用法)。

## 6. 提交与回主仓库

- 本分支 8 个提交未 push;推送走代理(§2)。合回 main 前**必须先和 Tab5 session
  协调**——main 的暂存区有它的回退+screen 角色工作,bridge.py/multi_agent.py
  在两边都改了,直接合必冲突。
- 提交风格:`git log stopwatch-walkie/` 为准,一提交一逻辑变更。
