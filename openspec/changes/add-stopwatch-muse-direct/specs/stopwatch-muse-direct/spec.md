## Purpose

规定 StopWatch 在直连 Muse 模式下的行为：不经 Mac，通过 Wi-Fi 直接用 HTTPS 访问个人云，完成“按住说话 → 转写 → 问 Muse → 显示回答”。

## ADDED Requirements

### Requirement: Direct mode is a separate build
直连模式 SHALL 是单独的编译环境 `m5stack-stopwatch-muse`。原有 `m5stack-stopwatch` 环境的行为 MUST NOT 改变。直连模式 MUST NOT 连接 Mac 桥，也 MUST NOT 提供指挥编码助手的功能。

#### Scenario: 烧录原有环境
- **WHEN** 用户烧录 `m5stack-stopwatch`
- **THEN** 手表照旧连接 Mac 桥，行为与改动前一致

### Requirement: Online means the personal cloud answered
直连模式下，Wi-Fi 连上之后，手表 SHALL 用设备令牌调用 `GET /device/v1/status`。返回 200 且 `transcribeConfigured` 为真时，进入就绪状态；否则停在连接中，或显示出错原因。空闲时 SHALL 每 60 秒检查一次；提问正在进行时，暂停后台检查。服务器证书 MUST 用内置的 ISRG Root X1、X2 校验，MUST NOT 关闭证书校验。

#### Scenario: 访客网需要网页登录
- **WHEN** Wi-Fi 已连上，但 HTTPS 请求被强制门户拦截，导致证书校验失败
- **THEN** 手表显示“连不上个人云”的出错页，不进入就绪状态

#### Scenario: 设备令牌失效
- **WHEN** 状态接口返回 401
- **THEN** 手表显示“设备需要重新配对”，不进入就绪状态

### Requirement: Push to talk, then upload once
在就绪状态按住 KEYA 时，手表 SHALL 把 16 kHz 单声道 16 位录音写入 PSRAM，最长 30 秒，超过就自动结束。松开 KEYA 时 SHALL 振动一次，进入转写中，并把整段录音以 `audio/wav` 上传到 `POST /device/v1/transcribe`。录音短于 0.5 秒时 MUST 直接丢弃，不上传。录音过程中按 KEYB SHALL 取消，丢弃录音，不发任何请求。

#### Scenario: 误触
- **WHEN** 用户按了一下 KEYA 就松开，不到 0.5 秒
- **THEN** 手表回到就绪状态，个人云没有收到转写请求

#### Scenario: 说满 30 秒
- **WHEN** 用户按住 KEYA 超过 30 秒
- **THEN** 录音在第 30 秒自动结束并上传，与松开按键的效果相同

### Requirement: Ask Muse once per utterance
转写返回非空文字后，手表 SHALL 用协议 2（请求头 `X-OpenMuse-Protocol: 2`）调用 `POST /device/v1/chat`，`briefChars` 取 40，并为这段录音生成一个固定的请求号。收到 202 时 SHALL 按 `Retry-After` 用同一个请求号重试，总等待不超过 60 秒；网络中断时 SHALL 用同一个请求号重试 1 次。转写结果为空时 MUST NOT 调用 `/v1/chat`。

#### Scenario: 没听清
- **WHEN** 转写返回空文字
- **THEN** 手表显示“没听清，请再说一次”，模型额度不变

#### Scenario: 网络抖动后重试
- **WHEN** 第一次 `/v1/chat` 请求在回答返回前断开，手表用同一个请求号重试
- **THEN** 个人云返回原回答或 202，模型额度只增加 1 次

### Requirement: Reading pages use large smooth text
确认、回答、提醒、出错四种页面 SHALL 是整屏的阅读页，只有三层：顶上一行小字标签、中间的正文、底部的按键提示。外圈颜色仍表示模式。
- 字体 SHALL 是抗锯齿的中文字体（思源黑体 SC，OFL 授权），正文 32 像素；32 像素放不下时整段改用 24 像素；24 像素也放不下时，才在最后一行末尾用“…”截断。字库覆盖全部 GB2312 汉字与常用符号；字库里没有的字显示为“□”。
- 正文 SHALL 按圆形屏幕每一行的实际宽度换行，并尽量让各行长度均匀，不把一个字单独留在最后一行；逗号、句号等标点 MUST NOT 出现在行首；英文单词和数字不拆开。
- 回答页：标签是灰色的“问：”加识别出的问题（超出屏宽用“…”截断），正文是完整的精简回答（`briefChars` 40），不再截成一行。
- 确认页：标签是“N 秒后发送”，正文是识别出的问题，外圈随倒计时缩短。
- 出错页：标签是“没发出去”（可重试）或“需要处理”，正文是原因。
- 按键提示用动作词，例如“再问”“好的”“现在发”“取消”“下一条”“知道了”“重说”“返回”。

阅读页顶部 SHALL 有一只缩小的螃蟹（每格 3 像素）作为状态图标，位于标签上方，不遮挡文字。主页、在听、在想、连接中 SHALL 是螃蟹页：大螃蟹（每格 9 像素）居中，下方是中文标题（“可以提问”“在听”“在想”“连接中”）、一行状态（如“有 2 条提醒”“正在识别…”“Muse 在想…”）和按键提示，不再使用带英文的插图。

收到回答时手表 SHALL 振动一次。直连模式不朗读。（2026-10-10 两次真机反馈：先是“订阅”被识别成“音乐”却看不出来，因此加上“问”标签；后是 12/16 像素点阵字太小发糊、提醒挤成一行，因此改成整屏大字。）

#### Scenario: 收到回答
- **WHEN** 识别出的问题是“明天有什么安排”，Muse 返回“明天上午九点有组会，十点前要交周报，下午三点体检。”
- **THEN** 回答页顶上显示灰色的“问：明天有什么安排”，中间用 32 像素分三行显示整句回答，手表振动一次

#### Scenario: 听错了
- **WHEN** 使用者说“我的订阅”，识别结果是“我的音乐”
- **THEN** 确认页正文用大字显示“我的音乐”，使用者在 3 秒内按 KEYB 取消，模型额度不变

#### Scenario: 很长的提醒条目
- **WHEN** 一条提醒有 110 个汉字
- **THEN** 正文改用 24 像素完整显示；只有 24 像素也放不下时，最后一行以“…”结尾

### Requirement: The crab acts every mode
螃蟹 SHALL 按 `openmuse-device-interaction` 的规定演出当前模式，每一帧只由“进入该模式多久”和当前时间决定（`include/muse_crab.h`）：
- 主页：落地时轻轻一弹，随后呼吸、眨眼，眼睛看向 KEYA；每 9 秒右钳开合两下；有未读提醒时身边有珊瑚色数字角标。
- 在听：先蹲 90 毫秒，再弹起双钳（略过头再回落），钳子随音量轻颤；两侧各四根声波条随麦克风音量变化。
- 在想：一只钳子托下巴、眼睛向上看并缓慢游移；头顶三个青色小点绕圈。
- 连接中：眯眼、一只钳子搭在眉上，眼睛追着外圈跑动的弧头，脚步交替。
- 确认：小螃蟹看着倒计时弧的末端。
- 回答：小螃蟹蹲一下后跳起，^ ^ 眼，双钳举起。
- 提醒：小螃蟹睁大眼跳一下，挥动一只钳子。
- 出错：小螃蟹眯眼、钳子放下、身体下沉。

画面 SHALL 只用整数矩形，不旋转像素；动画每秒 12.5 帧；文字层只在内容变化时重画，每帧只重画螃蟹、外圈和声波等动效区域。外圈颜色仍单独表示模式。

#### Scenario: 开始说话
- **WHEN** 使用者在主页按住 KEYA
- **THEN** 外圈变成珊瑚色，螃蟹先蹲一下再举起双钳，两侧声波随说话声起伏

### Requirement: Idle home dims
主页连续 2 分钟没有按键、也没有模式变化时，手表 SHALL 把屏幕调暗，螃蟹闭眼慢慢呼吸，帧率降到每秒 2.5 帧，以减少 AMOLED 烧屏和耗电。任意按键或模式变化 SHALL 立即恢复原亮度；按下的键照常生效。

#### Scenario: 放着不管
- **WHEN** 手表在主页放了 2 分钟
- **THEN** 屏幕变暗、螃蟹睡着；按住 KEYA 时屏幕恢复亮度并直接进入在听

### Requirement: Thinking can be abandoned and slow uploads give up
在想页 SHALL 显示“Ⓑ 取消”。在想时按 KEYB，手表 SHALL 立即回到主页；已经发出的请求在后台照常结束，但结果 MUST NOT 显示，也 MUST NOT 把手表切到出错页。上传录音超过 30 秒还没传完时，手表 SHALL 放弃这次上传（不重试），出错页显示“网络太慢，靠近路由器再试”，并保持在线，按住 KEYA 可以直接重说。串口的请求日志 SHALL 带上 Wi-Fi 信号强度（RSSI），Wi-Fi 连上时也打印一次。（2026-10-10 在家实测：信号弱时 93 KB 录音上传了 89 秒，手表一直停在在想，按键也退不出来。）

#### Scenario: 网络很慢
- **WHEN** 录音上传到第 30 秒还没传完
- **THEN** 出错页显示“网络太慢，靠近路由器再试”，串口打印已发送字节数与 RSSI，模型额度不变

#### Scenario: 不想等了
- **WHEN** 在想页显示中，使用者按 KEYB
- **THEN** 回到主页；稍后转写或回答返回时，屏幕不变，串口记录“ignored: abandoned”

### Requirement: Reminder card pages through its entries
主页有未读提醒时 SHALL 显示“有 N 条提醒”，按键提示出现“看提醒”。在主页按 KEYB SHALL 打开最新一张未读提醒卡，每个条目占一页，标签为“提醒 i/N”。在提醒页按 KEYB SHALL 翻到下一条；在最后一条按 KEYB（提示“知道了”）SHALL 关闭提醒卡并把它标记为已读。在提醒页按住 KEYA SHALL 关闭提醒卡（同样标记为已读）并开始新的提问。使用者翻阅期间，后台刷新 MUST NOT 替换正在看的提醒卡。StopWatch 只有两个键，因此不提供“稍后提醒”。

#### Scenario: 看完一张三条的提醒卡
- **WHEN** 主页显示“有 1 条提醒”，使用者按三次 KEYB 打开并翻完，再按一次 KEYB
- **THEN** 依次显示“提醒 1/3”“提醒 2/3”“提醒 3/3”，最后一次按键后回到主页，这张卡被标记为已读

### Requirement: Fonts live in their own flash partition
直连模式 SHALL 使用自己的分区表 `partitions_muse.csv`：程序区 3 MB（不做 OTA），其余闪存作为 `fonts` 分区，存放 `tools/muse-fonts/make_fonts.py` 生成的字库镜像（24 与 32 像素两套）。字形 SHALL 在绘制时从闪存读取，内存里只放字形索引。`nvs` 与 `otadata` 的位置 MUST 与原分区表相同。`fonts` 分区为空或损坏时，手表 SHALL 仍能使用，阅读页退回到原来的点阵字显示。原有 `m5stack-stopwatch` 环境的分区表 MUST NOT 改变。

#### Scenario: 只烧了程序没烧字库
- **WHEN** 新固件烧录后 `fonts` 分区还是空的
- **THEN** 串口打印“fonts partition is empty”，提问与提醒功能照常，文字用点阵字显示

### Requirement: Plain-language errors
任何一步失败时，手表 SHALL 进入出错页，显示一个简短的原因，按 KEYA 可以重试。原因至少要区分：连不上个人云、设备需要重新配对、今天的转写或提问次数用完、转写服务暂不可用、没听清。屏幕内容见“Reading pages use large smooth text”。串口日志 MUST NOT 输出录音内容、识别出的文字、回答正文或设备令牌。

#### Scenario: 提问额度用完
- **WHEN** `/v1/chat` 返回 429
- **THEN** 出错页显示“今天的提问次数用完了”

#### Scenario: 串口日志
- **WHEN** 开发者查看一次完整提问期间的串口日志
- **THEN** 只能看到阶段、状态码、字节数和耗时，看不到识别文字、回答或令牌

### Requirement: Recording level is measurable
每段录音结束时，手表 SHALL 在串口打印这段录音的音量统计：时长、峰值、平均音量（RMS，单位 dBFS），以及接近满幅的削波样本数，以便判断识别不准是否由音量过小或削波造成。统计只包含数字，MUST NOT 包含任何录音内容。

#### Scenario: 查看一次提问的音量
- **WHEN** 使用者说完一句话并松开 KEYA
- **THEN** 串口出现一行类似 `[muse] level ms=3300 peak_dbfs=-12.4 rms_dbfs=-38.0 clipped=0`，不含其他内容
