# 会话交接书 — 2026-07-19 21:45  [源:cheap-model]
> 触发:SessionEnd (prompt_input_exit)  |  session:026fcf60-1668-4626-80e5-1d72213a9476

# 交接书：小咪 CoreS3 语音桌宠项目

## 目标
把 StackChan CoreS3 改造成能直连阿里云百炼 `qwen-audio-3.0-realtime-flash` 的中文语音桌宠。摸屏唤醒、按住说话、松开听回答，全程不依赖 Mac/守护/蓝牙，只要 WiFi。

## 已完成

### 6 个 commit 已 push 到 `feat/tab5-esp-claw-agentfarm-integration`
| Commit | 内容 |
|--------|------|
| `720f928` | OpenSpec 四件套 + Python 协议原型（定 URL/音色/帧大小） |
| `1b6bd59` | 本地音频回环真机验证 |
| `8ef9bc3` | 云端对话 FSM + 独立播放任务（卡顿终解） |
| `44ce6c3` | 费用护栏（空闲/轮数上限）+ 全部验收 |
| `eb3b1ce` | 小咪矢量猫脸 + 改名（原"小抓"） |
| `f713877` | OpenSpec 归档，6 条需求合入规格基准 |

### 验收进度
- ✅ 27 项任务全部完成、设备验证
- ✅ OpenSpec 规格已闭环（proposal/design/specs/tasks 齐全）
- ✅ 真机丝滑体验：松手→首声 ~1.5s、零卡顿

## 当前状态

### 工作树
- 无游离改动（只有 `.snapshot` 和无关的 cardputer 目录未跟踪）
- OpenSpec change 已归档到 `archive/2026-07-19-cores3-voice-assistant/`

### 设备状态
- 固件完整可运行，已刻进真机
- 小咪现在能在桌上直连云端聊天
- **真机 `motion=0`（静止）**——用户暂缓打开动作

### 控制面板原型
- 做了可交互 HTML（artifact `879f3954`）
- 包含实时状态、对话记录、四个动作控件（摇头跳舞卡片）
- **未接真机、未进 repo**

## 改动的文件（绝对路径）

**核心固件**：
- `/Users/txie/OpenSourceProjects/hardware-buddies/claude-code-buddy/src/stackchan_voice/main.cpp` — 主循环、状态机
- `/Users/txie/OpenSourceProjects/hardware-buddies/claude-code-buddy/src/stackchan_voice/realtime_ws.cpp` — WebSocket 会话处理
- `/Users/txie/OpenSourceProjects/hardware-buddies/claude-code-buddy/src/stackchan_voice/audio_io.cpp` — 音频采集/播放（**独立任务**）
- `/Users/txie/OpenSourceProjects/hardware-buddies/claude-code-buddy/src/stackchan_voice/cat_face.cpp` — 猫脸渲染

**配置与工具**：
- `/Users/txie/OpenSourceProjects/hardware-buddies/claude-code-buddy/platformio.ini` — 编译配置
- `/Users/txie/OpenSourceProjects/hardware-buddies/claude-code-buddy/src/stackchan/settings.h/cpp` — NVS 运行时参数
- `/Users/txie/OpenSourceProjects/hardware-buddies/claude-code-buddy/tools/voice-prototype/*.py` — 桌面验证工具

**规格与文档**：
- `/Users/txie/OpenSourceProjects/hardware-buddies/claude-code-buddy/openspec/specs/cores3-voice-assistant/spec.md` — 规格基准（6 条需求）
- `/Users/txie/OpenSourceProjects/hardware-buddies/claude-code-buddy/openspec/changes/archive/2026-07-19-cores3-voice-assistant/` — 完整 proposal/design/tasks 存档

## 未决问题 / 下一步

### 三条待定路线
1. **让小咪动起来**
   - 打开 `motion` NVS 开关 + 加"说话时跳舞"逻辑到语音 FSM
   - 重烧固件（几分钟）
   - **决策点**：现在就做？还是等其他功能稳定？

2. **控制面板落地**
   - 设计设备端 LAN HTTP/JSON 接口（GET 状态、POST 命令）
   - 面板改用真实设备通信而非演示
   - **涉及文件**：需新增 `src/stackchan_voice/http_server.cpp` 或扩展现有协议

3. **pilot 层集成**
   - 接入 agent-fleet（舰队语音舵手）
   - 大副那层已现成，只差语音入口
   - **涉及文件**：可能需改 main.cpp 的初始化流程

### 技术债
- 无

## 关键约束与坑

### 已验证的协议参数
| 项 | 值 | 来源 |
|-----|-----|----|
| API | DashScope realtime | 阿里云百炼文档 |
| 模型 | qwen-audio-3.0-realtime-flash | 实测可靠 |
| 音色 | longpaopao_v3.6 | 实测稳定 |
| 帧大小 | 16 bit PCM @ 16kHz | 硬件一致 |
| vad_mode | manual（关服务端 VAD） | 本地检测更响应 |

### 硬核教训（跨会话记忆存档）

**喂喇叭必须独立 FreeRTOS 任务**
- 主循环被 TLS 解密卡 ~500ms
- 即使缓冲再大也救不了"没人喂"导致的静音
- 必须单独任务持续写 DMA 缓冲

**费用护栏三重防线**
1. 单次回答硬限 ≤3 句
2. 无对话时主动断开会话
3. 20 轮对话后重建会话

**真机已验证**
- 松手→首声 1.5s（包括网络往返 + 解密）
- 播放无卡顿（独立任务生效）
- WiFi 频繁切换无 crash（WebSocket 断重连）

## 备注
- 用户邮箱：Gillian_Perrybiv@graduate.org
- 会话日期：2026-07-19
- 分支：feat/tab5-esp-claw-agentfarm-integration
- 项目记忆已更新至：`/Users/txie/.claude/projects/-Users-txie-OpenSourceProjects-hardware-buddies/memory/`

_由 session-handoff 生成;接班 agent 从此文件开始。_
