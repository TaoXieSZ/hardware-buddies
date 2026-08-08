# StopWatch Walkie 语音模型选型研究

检索日期：2026-08-07
检索范围：阿里云百炼 / Model Studio 公开官方文档与模型信息页。控制台链接如需登录，不作为事实来源。

## 结论先行

StopWatch 对讲链路如果目标是“可调试、可逐步替换、可保留现有对讲状态机”，优先选分段链路：

1. ASR：`qwen-audio-3.0-asr-flash-streaming`
2. 对话 / Agent：复用现有本地 `voice-gateway` / Agent 会话路由，不让百炼语音模型接管权限和工具调用
3. TTS：首选 `qwen3-tts-instruct-flash-realtime`；只要求简单播报时可用 `qwen3-tts-flash-realtime`

如果目标是“最低交互延迟、直接语音进语音出、自然打断”，可以试验端到端：

1. `qwen-audio-3.0-realtime-flash`
2. 仅在语音对话质量明显不够时升级到 `qwen-audio-3.0-realtime-plus`

端到端方案的主要代价是上下文、工具调用、成本和音频协议都绑定在一个 WebSocket 会话里，调试和硬件侧排障会比三段式链路困难。

## doc id 2979031 识别结果

用户给的控制台文档链接是：

https://bailian.console.aliyun.com/cn-beijing/?spm=a2c4g.11186623.0.0.4ab7695bvInrXr&tab=doc#/doc/?type=model&url=2979031

公开官方文档没有提供 `url=2979031` 到模型 slug 的反查接口，控制台路由本身也可能需要登录态。因此不能把“2979031 对应哪个模型”作为一手可确认事实。

因此本文不猜测这个 doc id 对应的模型。能确认的是：本地 StackChan 示例把 MiniMax `speech-02-turbo` 用作 TTS，而当前 StopWatch 桥接端把 `qwen3-asr-flash` 用作 ASR；两者不是同一类模型。对 StopWatch 的实时链路，应比较 `qwen3-asr-flash-realtime` 与更新的 `qwen-audio-3.0-asr-flash-streaming`。见 [语音识别选型页](https://help.aliyun.com/zh/model-studio/asr-model/)。

## 本地 StackChan 配置推断

本节是 repo-local 只读检查结果，不属于阿里云官方事实；用于解释“StackChan 之前可能用什么链路”以及 StopWatch 迁移时应优先兼容什么音频形态。

本地证据：

- `stackchan-firmware/voice-agent/src/app_config.json.example:17` 到 `stackchan-firmware/voice-agent/src/app_config.json.example:21`：Agora Agent 示例配置的输出音频 codec 是 `G722`，并配置 `che.audio.acm_ptime=20`。
- `stackchan-firmware/voice-agent/src/app_config.json.example:23` 到 `stackchan-firmware/voice-agent/src/app_config.json.example:38`：LLM 示例是 `gpt-4o-mini`，`url` 占位为 Azure OpenAI endpoint。
- `stackchan-firmware/voice-agent/src/app_config.json.example:40` 到 `stackchan-firmware/voice-agent/src/app_config.json.example:45`：TTS 示例是 MiniMax，模型 `speech-02-turbo`，音色 `female-chengshuxin`。
- `stackchan-firmware/voice-agent/include/agora_credentials.h.example:20` 到 `stackchan-firmware/voice-agent/include/agora_credentials.h.example:35`：凭据模板也指向 Agora Conversational AI Agent，LLM 默认 `gpt-4o-mini`，TTS 默认 ElevenLabs，语音 codec 可选 `PCMU` 或 `G722`。
- `stackchan-firmware/voice-agent/src/main.cpp:223` 到 `stackchan-firmware/voice-agent/src/main.cpp:227`：当前固件加入 Agora RTC 时禁用内置 codec negotiation，使用 raw PCM，采样率 16000，单声道。
- `stackchan-firmware/voice-agent/src/main.cpp:315` 到 `stackchan-firmware/voice-agent/src/main.cpp:343`：音频任务是半双工 LISTEN/SPEAK 状态机，采集 20ms PCM chunk 后发送。

推断：

- StackChan 当前可见配置不是百炼原生 ASR/TTS/Realtime 链路，而是“设备 ↔ Agora RTC ↔ Conversational AI Agent ↔ LLM/TTS 后端”的架构；其中能明确确认的旧语音模型是 MiniMax `speech-02-turbo`，用途仅为 TTS。
- `doc id 2979031` 如果来自“StackChan 之前可能用的文档”，公开资料无法证明它已被当前本地 StackChan 配置实际使用；本地模板反而显示 LLM/TTS 示例来自 OpenAI/Azure、MiniMax 或 ElevenLabs。
- StopWatch 若从 StackChan 迁移，最应保留的是音频和交互约束：16kHz 单声道 PCM、20ms chunk、半双工 / push-to-talk、防止 TTS 回灌触发自问自答。
- 这与百炼 `qwen-audio-3.0-realtime-flash` 的输入 PCM 16kHz/16bit/单声道要求天然匹配；也与三段式 ASR WebSocket 的 `pcm` + `sample_rate=16000` 形态匹配。

## TTS 音色试听资产

本次调研生成的模型试听音频归档在 `./assets/`，用于在另一台电脑继续做听感对比：

- [`longanlingxi`](./assets/voice_longanlingxi.wav)
- [`longanqian`](./assets/voice_longanqian.wav)
- [`longanxiaoxin`](./assets/voice_longanxiaoxin.wav)
- [`longjielidou_v3.6`](./assets/voice_longjielidou_v3.6.wav)
- [`longpaopao_v3.6`](./assets/voice_longpaopao_v3.6.wav)

原始输入录音不进入公开仓库。

## 官方证据集

### 语音识别 ASR

[语音识别选型页](https://help.aliyun.com/zh/model-studio/asr-model/) 给出的当前推荐：

- `qwen-audio-3.0-asr-flash-streaming`：实时，WebSocket，支持热词和 Prompt 上下文，多语种及方言，音频最大时长无限制。
- `qwen3-asr-flash-realtime`：实时，WebSocket，支持情感识别，多语种及方言，音频最大时长无限制。
- `fun-asr-realtime`：实时，WebSocket，支持热词，多语种及方言。
- Paraformer 是较早一代模型；官方建议在业务允许时迁移到 Fun-ASR 或 Qwen-ASR。

[qwen-audio-3.0-asr-flash-streaming 模型信息](https://help.aliyun.com/zh/model-studio/qwen-audio-3-0-asr-flash-streaming)：

- 定位：低延迟、高并发实时交互，边说边出字，集成 Context 上下文增强，并强化垂直行业专业词汇。
- 模态：Audio -> Text。
- 华北2（北京）价格：0.00033 元/秒。
- 华北2（北京）限流：1200 RPM。

[qwen3-asr-flash-realtime 模型信息](https://help.aliyun.com/zh/model-studio/qwen3-asr-flash-realtime)：

- 定位：千问 3 ASR Flash 实时版，高精度、高鲁棒、多语种语音识别。
- 模态：Audio -> Text。
- 稳定版当前等同 `qwen3-asr-flash-realtime-2025-10-27`，另有 `2026-02-10` 快照。
- 华北2（北京）价格：0.00033 元/秒。
- 华北2（北京）限流：1200 RPM。

[qwen3-asr-flash 模型信息](https://help.aliyun.com/zh/model-studio/qwen3-asr-flash)：

- 定位：非实时 ASR；稳定版等同 `qwen3-asr-flash-2025-09-08`。
- 华北2（北京）价格：0.00022 元/秒。
- 华北2（北京）限流：100 RPM。
- 更适合上传短音频片段或离线补偿，不适合作为实时对讲主路径。

[实时语音识别 WebSocket API](https://help.aliyun.com/zh/model-studio/fun-asr-realtime-websocket-api)：

- `qwen-audio-3.0-asr-flash-streaming` / `fun-asr-realtime` 使用华北2（北京）业务空间专属域名：
  `wss://{WorkspaceId}.cn-beijing.maas.aliyuncs.com/api-ws/v1/inference`
- DashScope SDK 目前仅支持 Java 和 Python；其他语言需要直接 WebSocket。

[实时 ASR 客户端事件](https://help.aliyun.com/zh/model-studio/fun-asr-client-events)：

- `run-task` 指定 `task_group=audio`、`task=asr`、`function=recognition`、`model`、`format`、`sample_rate`。
- 支持音频格式：`pcm`、`wav`、`mp3`、`opus`、`speex`、`aac`、`amr`；`opus/speex` 必须 Ogg 封装，`wav` 必须 PCM 编码，`amr` 仅 AMR-NB。
- 8k 模型只支持 8000 Hz；其他模型支持任意采样率。
- `qwen-audio-3.0-asr-flash-streaming` 支持即时热词 `vocabulary`；权重 `[1,5]` 或 `50`，超级热词最多 50 个。
- VAD 断句默认更低延迟；语义断句准确性更高但更适合会议转写。

[实时 ASR 服务端事件](https://help.aliyun.com/zh/model-studio/fun-asr-server-events)：

- `result-generated` 同时返回中间结果与最终结果，最终结果有 `sentence_end=true`。
- 结果包含句级时间戳、字级时间戳和计费时长 `usage.duration`。

### 对话 / Agent

[qwen-flash 模型信息](https://help.aliyun.com/zh/model-studio/qwen-flash)：

- 文本输入 / 文本输出。
- 华北2（北京）支持 Function Calling、结构化输出、联网搜索、上下文缓存、批量推理。
- 上下文长度 1,000,000 tokens。
- 华北2（北京）输入 <=128k 时：输入 0.15 元/百万 tokens，输出 1.5 元/百万 tokens。
- 华北2（北京）限流：60 RPM、1,000,000 TPM。
- 适合 StopWatch 的轻量意图识别、命令解释、简短回复生成。

[qwen-plus 模型信息](https://help.aliyun.com/zh/model-studio/qwen-plus)：

- 文本输入 / 文本输出。
- 华北2（北京）支持 Function Calling、结构化输出、联网搜索、上下文缓存、批量推理。
- 上下文长度 1,000,000 tokens。
- 华北2（北京）输入 <=128k 时：输入 0.8 元/百万 tokens，输出 2 元/百万 tokens；思考输出 8 元/百万 tokens。
- 更适合多步推理、复杂回复、较高可靠性意图解析。

[新版智能体应用 Agent 2.0](https://help.aliyun.com/zh/model-studio/new-single-agent-application)：

- Agent 2.0 将知识库、MCP 等能力统一为工具，并通过自主思考和规划调用。
- 官方建议没有旧版本依赖时使用新版。
- 为确保多步规划效果，官方建议选用具备强工具调用能力的模型，如千问 Max 系列。

[应用 DashScope API 参考](https://help.aliyun.com/zh/model-studio/agent-and-workflow-application-api-reference)：

- 该应用 API 文档仅适用于华北2（北京）。
- HTTP 调用地址：`POST https://dashscope.aliyuncs.com/api/v1/apps/APP_ID/completion`
- 使用前需要创建应用、获取应用 ID 和 API Key。

StopWatch 判断：如果只是“按键对讲 + 状态控制 + 轻量问答”，不建议一开始上 Agent 2.0；本地应用层显式控制工具调用会更可测。Agent 2.0 适合后续接入知识库、MCP 工具和多步骤规划。

### TTS

[语音合成选型页](https://help.aliyun.com/zh/model-studio/tts-model/)：

- WebSocket 是双向流式通信，支持流式输入和流式输出，音频边合成边返回，延迟最低，适合语音助手、智能客服、呼叫中心等实时交互场景。
- HTTP 是完整文本输入、流式返回音频，适合有声阅读和内容制作。
- Qwen-Audio-TTS / CosyVoice 同一模型名同时支持 WebSocket 和 HTTP；Qwen-TTS 通过 `-realtime` 后缀区分 WebSocket 模型。

[qwen3-tts-flash-realtime 模型信息](https://help.aliyun.com/zh/model-studio/qwen3-tts-flash-realtime)：

- 定位：低延迟、高稳定实时语音合成；支持多语言、方言和同一音色多语言输出。
- 稳定版当前等同 `qwen3-tts-flash-realtime-2025-11-27`。
- 华北2（北京）价格：1 元/万字符。
- 华北2（北京）限流：180 RPM。

[qwen3-tts-instruct-flash-realtime 模型信息](https://help.aliyun.com/zh/model-studio/qwen3-tts-instruct-flash-realtime)：

- 通过自然语言控制合成效果；目前支持 25 个音色的中英文 Instruct 调节。
- 华北2（北京）价格：1 元/万字符。
- 华北2（北京）限流：180 RPM。
- 如果 StopWatch 需要“紧张、开心、播报、低声”等显式情绪/风格控制，优先试这个。

[qwen-audio-3.0-tts-flash 模型信息](https://help.aliyun.com/zh/model-studio/qwen-audio-3-0-tts-flash)：

- 定位：面向实时交互场景优化，支持更多小语种和中文方言，增强 free-style 指令遵循与细粒度标签控制。
- 官方称 Flash 版本首包延时控制在 200ms 以内。
- 华北2（北京）价格：1 元/万字符。
- 华北2（北京）限流：180 RPM。

[Qwen-TTS-Realtime WebSocket API](https://help.aliyun.com/zh/model-studio/interactive-process-of-qwen-tts-realtime-synthesis)：

- 华北2（北京）WebSocket：`wss://dashscope.aliyuncs.com/api-ws/v1/realtime?model=qwen3-tts-flash-realtime`
- 鉴权通过 WebSocket 请求头 `Authorization: Bearer <api_key>`。

[Qwen-Audio-TTS/CosyVoice WebSocket API](https://help.aliyun.com/zh/model-studio/cosyvoice-websocket-api)：

- 华北2（北京）业务空间专属域名：
  `wss://{WorkspaceId}.cn-beijing.maas.aliyuncs.com/api-ws/v1/inference`
- 官方建议从旧 `dashscope.aliyuncs.com` 迁移到业务空间专属域名以提升性能和稳定性。

[Qwen-Audio-TTS/CosyVoice 客户端事件](https://help.aliyun.com/zh/model-studio/cosyvoice-client-events)：

- 支持输出编码：`pcm`、`wav`、`mp3`（默认）、`opus`。
- 支持采样率：8000、16000、22050（默认）、24000、44100、48000 Hz。
- 支持音量、语速、音调、码率；`qwen-audio-3.0-tts-flash` 支持通过 `instruction` 控制方言、情感或角色等效果。

### 端到端实时语音对话

[Qwen-Audio 实时语音对话用户指南](https://help.aliyun.com/zh/model-studio/qwen-audio-realtime-user-guides)：

- Qwen-Audio 是端到端实时语音交互大模型，通过 WebSocket 流式协议实现低延迟语音对话。
- 工作方式：客户端持续发送麦克风音频流，服务端实时返回语音和文本响应，无需轮询。
- 输入音频：PCM，16kHz，16bit，单声道。
- 输出音频：PCM，24kHz，16bit，单声道。
- `qwen-audio-3.0-realtime-plus` 和 `qwen-audio-3.0-realtime-flash` 默认历史 20 轮，最大 50 轮；上下文保留音频累计最大 300 秒。

[Qwen-Audio Realtime WebSocket API](https://help.aliyun.com/zh/model-studio/fun-audiochat-realtime-websocket-api)：

- 华北2（北京）服务端点：
  `wss://{WorkspaceId}.cn-beijing.maas.aliyuncs.com/api-ws/v1/realtime?model=<model_name>`
- 官方建议从 `dashscope.aliyuncs.com` 迁移到 `{WorkspaceId}.cn-beijing.maas.aliyuncs.com`。
- 请求头使用 `Authorization: Bearer <api_key>`，可选 `X-DashScope-WorkSpace`。

[Qwen-Audio Realtime 客户端事件](https://help.aliyun.com/zh/model-studio/fun-audiochat-client-events)：

- `session.update` 可设置 `modalities`、`voice`、`turn_detection`。
- 支持 Function Calling 工具定义，模型可根据用户输入自主决定是否调用工具。
- 支持三种交互模式：`server_vad`、`smart_turn`、push-to-talk。
- push-to-talk 对应 `turn_detection=null`，适合按键说话和精确控制。
- 可通过 `conversation.item.create` 注入用户文本或写回 `function_call_output`。

[qwen-audio-3.0-realtime-flash 模型信息](https://help.aliyun.com/zh/model-studio/qwen-audio-3-0-realtime-flash)：

- 输入 / 输出模态：Audio、Text。
- 支持 Function Calling。
- 上下文长度 40960 tokens。
- 华北2（北京）价格：输入音频 30 元/百万 tokens，输入文本 3 元/百万 tokens，输出文本 30 元/百万 tokens，输出文本+音频 100 元/百万 tokens。
- 华北2（北京）限流：60 RPM、100,000 TPM。

[qwen-audio-3.0-realtime-plus 模型信息](https://help.aliyun.com/zh/model-studio/qwen-audio-3-0-realtime-plus)：

- 输入 / 输出模态：Audio、Text。
- 支持 Function Calling。
- 上下文长度 40960 tokens。
- 华北2（北京）价格：输入音频 40 元/百万 tokens，输入文本 5 元/百万 tokens，输出文本 40 元/百万 tokens，输出文本+音频 150 元/百万 tokens。
- 华北2（北京）限流：60 RPM、100,000 TPM。

[模型价格总页](https://www.alibabacloud.com/help/zh/model-studio/model-pricing) 对实时语音对话补充说明：

- 音频 Token 按时长折算：总 Token 数 = 音频时长（秒） * 12.5，不足 1 秒按 1 秒计。
- 多轮对话中，历史对话会作为后续轮次输入继续计费；建议控制单次会话轮数或适时新开会话。

按上述折算，`qwen-audio-3.0-realtime-flash` 的粗略音频成本约为：

- 输入音频：30 元 / 1,000,000 tokens * 12.5 tokens/s = 0.000375 元/秒
- 输出音频：100 元 / 1,000,000 tokens * 12.5 tokens/s = 0.00125 元/秒

实际总成本还会叠加文本输入、文本输出和历史上下文重复计费。

## StopWatch 链路推荐

### 推荐 A：可控三段式，适合先落地

链路：

1. 设备 / 桥接进程采集音频。
2. ASR WebSocket 调 `qwen-audio-3.0-asr-flash-streaming`。
3. 应用层用本地状态机把文本路由到现有 `voice-gateway` / Agent 会话，并保留人工授权边界。
4. TTS WebSocket 首选 `qwen3-tts-instruct-flash-realtime`，用自然语言约束“短句、清楚、像对讲机”；无需风格控制时用 `qwen3-tts-flash-realtime`。
5. 音频回放到 StopWatch / Mac / 桥接设备。

为什么推荐：

- ASR、LLM、TTS 分离，便于定位延迟来自采音、ASR、推理、合成还是播放。
- 现有硬件桥接项目可以保留显式状态机和 push-to-talk 语义。
- ASR 可以使用即时热词和上下文，适合“按钮名、设备名、会话名、专有术语”。
- TTS 可独立换音色、采样率和编码格式。

推荐参数：

- ASR 输入：设备侧优先输出 PCM 16kHz 单声道；如果带宽敏感再评估 Ogg Opus。
- ASR 断句：对讲场景先用 VAD 断句，`max_sentence_silence` 从 800-1300ms 区间实测。
- TTS 输出：若直接播放，优先 PCM 或 WAV；若走 BLE / 网络传输，评估 Opus。
- Agent：沿用本地 Agent 会话实际配置的模型；语音桥只负责转写、路由和播报，不另起一套百炼 LLM。

主要风险：

- 三段链路总延迟由 ASR 尾延迟 + LLM 首 token + TTS 首包叠加，需要端到端实测。
- WebSocket 会话、重连、心跳、半句取消需要应用层自己处理。
- ASR 和 TTS 的业务空间专属域名依赖 `WorkspaceId`，部署配置要按北京地域固定。

### 推荐 B：端到端 Qwen-Audio Realtime，适合低延迟实验

链路：

1. 设备 / 桥接进程采集 16kHz 16bit mono PCM。
2. WebSocket 调 `qwen-audio-3.0-realtime-flash`。
3. 使用 push-to-talk：`turn_detection=null`。
4. 需要自然打断再试 `smart_turn`。
5. 通过 Function Calling 接入本地工具。

为什么推荐做实验：

- 单模型完成语音理解、回复生成和语音输出，理论上交互更自然。
- 官方支持服务端 VAD、smart_turn 和 push-to-talk，push-to-talk 与对讲硬件更贴合。
- 返回文本和音频，便于屏幕字幕与语音播放同步。

主要风险：

- 音频协议固定为输入 PCM 16kHz、输出 PCM 24kHz；设备侧可能需要重采样。
- 输出音频按 token 计费，且历史上下文会滚入后续输入，长会话成本更难预测。
- 上下文最大 50 轮 / 300 秒音频，长时间陪伴或连续对讲要设计会话切分。
- 端到端模型虽然支持 Function Calling，但工具调用调试不如文本模型显式链路透明。

### 不推荐作为主路径

- `qwen3-asr-flash`：非实时 ASR，适合短音频补偿或离线复核，不适合实时对讲主路径。
- Paraformer：官方已标注为较早一代，业务允许时建议迁移到 Fun-ASR 或 Qwen-ASR。
- 旧 Assistant API：官方标注“下线中”，建议迁移 Responses API；StopWatch 如用百炼应用，应优先看 Agent 2.0 / 应用 API。

## 最小验证计划

1. 先做 ASR 单测：设备音频 -> `qwen-audio-3.0-asr-flash-streaming`，记录首字延迟、最终句延迟、错词、断句。
2. 再做 TTS 单测：固定 10 条短回复 -> `qwen3-tts-flash-realtime` 和 `qwen-audio-3.0-tts-flash`，记录首包、总时长、音色接受度。
3. 串三段链路：ASR -> `qwen-flash` -> TTS，目标是按键说话后 1-2 秒内开始播报。
4. 并行试端到端：`qwen-audio-3.0-realtime-flash` push-to-talk，同一批语料比较首响、打断、工具调用和成本。
5. 如果三段链路总延迟不可接受，再把端到端作为主线；如果端到端工具/状态控制不稳，保持三段式。

## 版本与时效风险

- 本文事实截至 2026-08-07。百炼模型价格、免费额度、限流、稳定版到快照版映射都属于高时效信息，上线前应重新核对模型信息页。
- `help.aliyun.com` 模型信息页以人民币“元”展示；`alibabacloud.com/help/zh` 总价页可能以美元符号展示同一价格口径。本文优先使用模型信息页的“元”价格；仅在实时语音对话的音频 token 折算规则上引用总价页。
- 控制台 `url=2979031` 无公开官方反查结果，不能作为实现或文档中的硬编码依据。实现应使用公开模型 ID，例如 `qwen-audio-3.0-asr-flash-streaming`、`qwen3-tts-flash-realtime`、`qwen-audio-3.0-realtime-flash`。
