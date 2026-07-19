# cores3-voice-assistant Spec Delta

## ADDED Requirements

### Requirement: 独立语音助手固件 env
系统 SHALL 提供 PlatformIO env `cores3-stackchan-voice`，构建产物为独立的 StackChan
语音助手固件；该 env 的加入 MUST NOT 改变现有 `cores3-stackchan*` 等任何 env 的
编译结果与运行行为。

#### Scenario: 独立构建
- **WHEN** 运行 `pio run -e cores3-stackchan-voice`
- **THEN** 构建成功，产物不包含 buddy 主入口（BLE 桥 / daemon 心跳逻辑）

#### Scenario: 不影响 buddy 固件
- **WHEN** 运行 `pio run -e cores3-stackchan-claude`
- **THEN** 构建结果与本变更引入前一致（不引入语音助手模块）

### Requirement: PTT 按住说话采集
固件 SHALL 以按住触摸屏为录音手势：按住期间以 16 kHz mono PCM 采集麦克风，
松开即结束本轮输入。播放回复期间麦克风 MUST 保持关闭（半双工）。

#### Scenario: 按住采集、松开提交
- **WHEN** 用户在 READY 态按住触摸屏说话后松开
- **THEN** 固件在按住期间流式上传音频帧，松开后提交本轮并请求模型响应

#### Scenario: 播放期间闭麦
- **WHEN** StackChan 正在播放模型回复
- **THEN** 麦克风不采集，触摸不触发新一轮录音（忽略或排队至播放结束）

### Requirement: DashScope Realtime 会话（Manual 模式）
固件 SHALL 通过 TLS WebSocket 直连 DashScope Realtime API，使用
qwen-audio-3.0-realtime-flash 模型与 Manual 轮次模式（`turn_detection: null`）：
上行 `input_audio_buffer.append`（base64 PCM 16 kHz）→ `commit` → `response.create`，
下行解析 `response.audio.delta`（base64 PCM 24 kHz）直至 `response.done`。
会话建立时 MUST 通过 `session.update` 下发人设 instructions 与音色。

#### Scenario: 一轮对话往返
- **WHEN** 固件提交一轮语音输入
- **THEN** 收到的 `response.audio.delta` 音频以 24 kHz 经喇叭播放，`response.done`
  后回到 READY 态

#### Scenario: 人设生效
- **WHEN** 会话建立后用户提问
- **THEN** 回复为中文萌系桌宠口吻（instructions 约束单次回答不超过 3 句）

### Requirement: 懒连接与空闲断开
固件 SHALL 仅在需要时建立 WebSocket 连接（SLEEP 态被触摸唤醒时发起），连接在
空闲超过可配置时长（默认 5 分钟）后 MUST 主动断开并回到 SLEEP 态；断开即丢弃
对话上下文。同一连接内对话轮数达到可配置上限（默认 20 轮）时 MUST 重建 session
以防上下文累积计费失控。

#### Scenario: 懒连接
- **WHEN** 设备处于 SLEEP 态且用户触摸屏幕
- **THEN** 固件建立 WSS 连接并在就绪后进入 READY 态；SLEEP 期间无网络连接

#### Scenario: 空闲断开
- **WHEN** READY 态持续无交互达到空闲超时
- **THEN** 固件断开 WebSocket、显示打盹表情，不再产生任何 API 流量

### Requirement: 表情与动作联动
固件 SHALL 将会话状态映射到表情/动作：SLEEP=打盹、LISTENING=注意（倾听）、
THINKING=忙碌、SPEAKING=说话嘴型（按播放活动驱动）。映射 MUST 由本地状态驱动，
不依赖模型输出任何控制指令。

#### Scenario: 说话动嘴
- **WHEN** 回复音频正在播放
- **THEN** StackChan 呈现说话表情/嘴型，播放结束后恢复 READY 表情

### Requirement: 故障表现与凭据缺失
凭据缺失（占位符 API key）、WiFi 不可用或 WebSocket 连接/会话出错时，固件 MUST
保持可运行（表情待机不崩溃），以简短屏幕提示和串口日志说明原因；连接类错误在
下一次触摸唤醒时 SHALL 重试而非自动重连风暴。

#### Scenario: 无 API key
- **WHEN** 固件以占位符 `dashscope_key` 启动且用户触摸唤醒
- **THEN** 屏幕提示缺少凭据，串口输出警告，设备保持待机不重启

#### Scenario: 会话中断网
- **WHEN** 对话过程中 WiFi 或 WSS 断开
- **THEN** 固件停止当前轮次、提示离线并回到 SLEEP，下次触摸重新懒连接
