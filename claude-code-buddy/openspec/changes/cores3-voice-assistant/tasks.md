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

- [ ] 4.1 抄 arduinoWebSockets `examples/` 的 WSS 初始化 verbatim，内置阿里云根 CA，
      连通并完成 `session.update`（人设+音色）
- [ ] 4.2 上行：按住期间 ~100 ms 分帧 base64 `append`，松开发 `commit`+`response.create`
- [ ] 4.3 下行：类型嗅探 + 流式 base64 解码入 ring buffer 播放；小事件走 ArduinoJson；
      `response.done` 收轮
- [ ] 4.4 每轮串口打印 usage/token 统计（费用观察）

## 5. 状态机与联动

- [ ] 5.1 会话状态机：SLEEP/CONNECTING/READY/LISTENING/THINKING/SPEAKING + 触摸唤醒、
      懒连接、空闲超时断开（默认 5 min）、轮数上限重建 session（默认 20）
- [ ] 5.2 表情/动作映射接入状态机（打盹/倾听/忙碌/说话嘴型），SPEAKING 按播放活动驱动
- [ ] 5.3 故障路径：占位符 key、WiFi 失败、WSS 断开的屏幕提示 + 串口日志 + 触摸重试
- [ ] 5.4 空闲超时与轮数上限接入 `settings.cpp` 可调项

## 6. 真机验收与收尾

- [ ] 6.1 真机烧录（按 MAC 认口：CoreS3 非 Tab5/cardputer），完整对话冒烟：
      唤醒→连续 3 轮问答→打盹→再唤醒
- [ ] 6.2 按 spec 场景逐条验收（独立构建、闭麦、懒连接、空闲断开、无 key、断网）
- [ ] 6.3 `make test` 全绿；README/docs 补一段语音助手固件说明（含费用护栏说明）
- [ ] 6.4 用户 review + commit（不 push）
