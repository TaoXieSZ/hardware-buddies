# Tasks: cores3-voice-assistant

## 1. Mac Python 协议原型（定案协议常量）

- [x] 1.1 用户在百炼控制台开通模型并创建 DashScope API key（北京地域），写入 ~/.dashscope-key
- [x] 1.2 `tools/voice-prototype/`：最小 WebSocket 客户端连通 Realtime API，确认 wss URL
      形态、`qwen-audio-3.0-realtime-flash` 模型 ID、鉴权 header
- [x] 1.3 原型跑通 Manual 模式一轮往返（本地 wav → append/commit/response.create →
      落盘 24 kHz 回复并播放），记录 `response.audio.delta` 单帧典型大小与事件序列
- [x] 1.4 音色试听（本模型 longan/loong 系音色表）+ 萌系人设 instructions 调词
      （含"回答 ≤3 句"），用户拍板音色与人设文案 → 定 longpaopao_v3.6
- [x] 1.5 将实测协议常量（URL/模型 ID/事件名/帧大小/usage 字段）回填 design.md

## 2. 固件脚手架

- [x] 2.1 `platformio.ini` 新增 `[env:cores3-stackchan-voice]`（继承 cores3 基础配置，
      build_src_filter 换入 `stackchan_voice/`，排除 buddy main.cpp；lib_deps 增加
      arduinoWebSockets 并锁版本）
- [x] 2.2 `wifi_secrets.ini` 占位符新增 `dashscope_key`，经 `-DSTACKCHAN_DASHSCOPE_KEY`
      注入；占位符时构建仍须通过
- [x] 2.3 `src/stackchan_voice/main.cpp` 骨架：M5 init + 复用 character/motion/settings，
      SLEEP 打盹表情可跑；验证 `pio run -e cores3-stackchan-voice` 与
      `pio run -e cores3-stackchan-claude` 均绿（2026-07-18 实测 2 env SUCCESS，
      voice: RAM 27.4% / Flash 18.2%）

## 3. 音频链路（先本地闭环，不接云）

- [x] 3.1 抄 M5Unified CoreS3 mic 示例 verbatim：PTT 按住触摸屏采集 16 kHz mono PCM，
      松开结束；串口打印采样统计验证（教训：record() 必须一块/tick，贪心循环会阻塞
      loop 饿死松手检测——见 audio_io.cpp 注释）
- [x] 3.2 Mic/Speaker 半双工切换封装（Speaker.end→Mic.begin 与反向），实测无爆音/卡死
      （切换时一行良性 I2S uninstall 日志，Microphone.ino 同款）
- [x] 3.3 下行播放：playRaw 2 槽队列照抄 audio_play.cpp；缓冲改 PSRAM 线性双指针
      （云端突发下发会冲掉 overwrite-oldest ring，偏差理由见 audio_io.h 注释）
- [x] 3.4 回环 demo 真机验证（2026-07-18）：1.64s / 3.06s 两轮录放，松手即停，
      时长与按住一致，回放清晰（音量夹逼 ≥96 运行时生效）

## 4. Realtime WebSocket 客户端

- [x] 4.1 抄 arduinoWebSockets `examples/` 的 WSS 初始化 verbatim，内置阿里云根 CA
      （GlobalSign R3，从 macOS 信任库提取），连通并完成 `session.update`（人设+音色）；
      真机 session ready ~1.1s。库 15KB 帧上限用 pre 脚本补丁到 48KB（见 design 排障 5）
- [x] 4.2 上行：按住期间 100ms 分帧 base64 `append` 边录边传，松开发
      `commit`+`response.create`；真机松手→首声 1.2-1.9s
- [x] 4.3 下行：类型嗅探 + base64 解码直入 PSRAM 播放缓冲；播放由独立 FreeRTOS
      任务喂（卡顿连环坑与终解见 design.md 排障记录 1-4）；`response.done` 收轮。
      2026-07-19 用户确认全程丝滑
- [x] 4.4 每轮串口打印 usage/token 统计 + underrun 卡顿账单（`[meter]`/`[underrun]`
      仪表保留，费用与音质双观察）

## 5. 状态机与联动

- [x] 5.1 会话状态机：SLEEP/CONNECTING/READY/LISTENING/THINKING/SPEAKING + 触摸唤醒、
      懒连接、空闲超时断开（默认 5 min）、轮数上限重建 session（默认 20，
      达上限显示"记性满啦，翻篇中……"并重连取新会话）
- [x] 5.2 表情/动作映射接入状态机（打盹/倾听/忙碌/说话），随 FSM 真机验证
- [x] 5.3 故障路径：占位符 key、WiFi 失败（附 status 码串口日志）、WSS 断开的屏幕
      提示 + 触摸重试——WiFi 打错 SSID 与断链场景均真机踩过并按 spec 表现
- [x] 5.4 空闲超时与轮数上限接入 `settings.cpp` 可调项（NVS 键 `vidle` 30-3600s /
      `vturns` 1-100；additive，buddy env 编译验证不受影响）

## 6. 真机验收与收尾

- [x] 6.1 真机烧录（按序列号认口 44:1B:F6=CoreS3），完整对话冒烟通过；
      串口证据：turn done underruns=0 dry=0ms（2026-07-19，连续两轮）
- [x] 6.2 spec 场景验收：独立构建/不影响 buddy（双 env 绿）、闭麦、懒连接、
      空闲断开、无 key 提示、断网提示均验证（断网/WiFi 失败在开发中真机踩过；
      2026-07-19 用户过验收清单确认"都是对的"）
- [x] 6.3 `make test` 全绿（2026-07-19：pytest + native Unity 40/40）；README
      "StackChan (CoreS3)" 下新增语音助手固件小节（刷法/交互/费用护栏/排障指引）
- [x] 6.4 用户 review + commit + push（2026-07-19，用户授权推 origin）

## 7. 增量：小咪猫脸 + 改名（2026-07-19 用户追加）

- [x] 7.1 `cat_face.{h,cpp}`：黑白美短猫脸纯矢量渲染，经典 StackChan 特写风格
      （整屏即脸，只画放大的大眼+ω 嘴，斑块从屏幕边缘溢出）+ 状态表情
      （打盹眯眼/倾听大眼/思考上瞟/说话张嘴+吐舌/眨眼）+ efontCN_24 字幕滚动带；
      弃用 GIF 角色包与 LittleFS（voice env 不再需要 uploadfs）
- [x] 7.2 人设改名 小抓 → 小咪（固件 instructions / 原型脚本 / README 同步）
- [x] 7.3 真机验收：特写猫脸颜值 + 表情状态切换 + 字幕可读性，2026-07-19 用户拍板
