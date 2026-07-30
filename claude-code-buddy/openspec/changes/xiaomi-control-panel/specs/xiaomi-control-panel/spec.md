# xiaomi-control-panel Spec Delta

## ADDED Requirements

### Requirement: 设备端面板托管
小咪固件 SHALL 在设备上运行一个 HTTP 服务，通过局域网同时提供控制面板网页与 JSON API。
网页 MUST 内嵌在固件中（不依赖 LittleFS），使得一次固件烧录即可使用。小咪采用懒连接——
开机后直到**首次唤醒**才关联 WiFi——因此 HTTP 服务 SHALL 在首次连上 WiFi 之后启动。
WiFi 关联在回到打盹时 MUST NOT 断开（打盹只断云端会话），故面板在首次唤醒后
SHALL 持续可访问，包括小咪打盹期间。

#### Scenario: 打开面板
- **WHEN** 小咪已唤醒并连上 WiFi，用户在同网段浏览器访问 `http://<设备IP>/`
- **THEN** 返回控制面板网页，页面能展示当前状态与设置

#### Scenario: 打盹期间仍可调参
- **WHEN** 小咪唤醒过一次后回到打盹态
- **THEN** 面板仍可访问，状态显示为"打盹"，设置仍可读写

#### Scenario: 开机未唤醒过
- **WHEN** 设备刚开机、尚未被唤醒（WiFi 未关联）
- **THEN** 面板地址不可访问；设备不因此报错或重启

#### Scenario: 找得到面板地址
- **WHEN** 小咪首次连上 WiFi
- **THEN** 屏幕字幕带短暂显示面板地址（含设备 IP）

### Requirement: 实时状态查询
系统 SHALL 提供 `GET /api/state`，返回当前会话状态（打盹/连接中/待命/倾听/思考/说话）、
WiFi 与 DashScope 连接状态、当前连接内轮次、上一轮的松手→首声延迟与 token 用量、
本轮 underrun 计数、电量与当前字幕。状态 MUST 取自主循环发布的快照，不得返回半更新的状态。

#### Scenario: 状态随对话变化
- **WHEN** 用户按住屏幕说话期间轮询 `/api/state`
- **THEN** 返回的状态为"倾听"，松手后依次变为"思考""说话"，回答结束回到"待命"

#### Scenario: 用量可观察
- **WHEN** 一轮对话结束后查询 `/api/state`
- **THEN** 返回该轮的 token 用量与首声延迟，且 underrun 计数可见

### Requirement: 设置读写与生效规则
系统 SHALL 提供 `GET /api/settings` 读取全部可调项，`POST /api/settings` 部分更新。
写入 MUST 校验取值范围、持久化到 NVS、并返回应用后的实际值。生效时机分两类：
音量、亮度、动作三项（总开关/待机张望/抬头角度）、空闲断开秒数、轮数上限 SHALL **即时生效**；
音色与人设 instructions SHALL 在**下次建立会话时生效**（MUST NOT 重连已建立的会话，
以免丢失上下文并产生额外计费），且响应中 MUST 标明该项为延迟生效。

#### Scenario: 即时生效项
- **WHEN** 面板把音量从 160 改成 200
- **THEN** 设备音量立即变化，重启后仍为 200

#### Scenario: 延迟生效项
- **WHEN** 对话进行中，面板把音色改成另一个
- **THEN** 当前会话仍用旧音色说完，响应标明"下次会话生效"；下次唤醒后的回答使用新音色

#### Scenario: 非法取值被拒
- **WHEN** 面板提交超出范围的值（如轮数上限 999）
- **THEN** 设备夹到合法范围并在响应中返回夹后的实际值，不写入非法值

### Requirement: 动作触发
系统 SHALL 提供 `POST /api/action` 触发一次性动作：`dance` 让小咪摆头跳一段，
`disconnect` 立即断开云端连接回到打盹。当摇头跳舞总开关为关时，`dance` MUST 不驱动舵机
并在响应中说明原因。

#### Scenario: 试跳一下
- **WHEN** 摇头跳舞开启，面板触发 `dance`
- **THEN** 小咪摆头跳一段后回到当前状态对应的动作

#### Scenario: 动作关闭时试跳
- **WHEN** 摇头跳舞关闭，面板触发 `dance`
- **THEN** 舵机不动，响应说明动作已关闭

### Requirement: 不干扰语音链路
HTTP 服务 MUST NOT 降低对话质量：面板被访问时 SHALL NOT 产生播放 underrun，
SHALL NOT 影响 PTT 松手检测。为此 HTTP 请求处理 MUST 在独立任务中进行，
且跨任务的设置变更 MUST 由主循环应用，不得在 HTTP 任务中直接操作外设或 NVS。

#### Scenario: 边对话边看面板
- **WHEN** 用户在对话过程中持续轮询面板状态
- **THEN** 该轮对话的 underrun 计数仍为 0，松手能正常结束录音
