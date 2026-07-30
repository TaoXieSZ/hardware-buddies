## ADDED Requirements

### Requirement: 设备端笔记列表覆盖层

固件 SHALL 在 NORMAL 态提供一个专用键（`l`）弹出笔记列表覆盖层，列出 SD 根目录下的
`note_*.wav` 文件（可滚动、可选中），并 SHALL 复用现有会话列表的交互（`,`/`.` 滚动、
`esc`/`` ` `` 关闭）。该覆盖层 SHALL 与审批/问答/会话/帮助覆盖层同级排他（打开时键盘归其独占）。
列表为空时 SHALL 显示占位提示（如 `(no notes)`）。

#### Scenario: 打开笔记列表

- **WHEN** NORMAL 态按 `l`
- **THEN** 固件 SHALL 扫描 SD 根目录，弹出 `note_*.wav` 的可滚动列表
- **AND** `,`/`.` SHALL 移动选中项，`esc`/`` ` `` SHALL 关闭覆盖层

#### Scenario: 空列表

- **WHEN** SD 上没有 `note_*.wav`（或 SD 未挂载）
- **THEN** 固件 SHALL 显示占位提示而非空白/崩溃

### Requirement: 就地流式回放选中笔记

固件 SHALL 在列表中 `enter` 选中项时回放该笔记：`file.seek(44)` 跳过 WAV 头后**分块**读 SD →
`M5Cardputer.Speaker.playRaw(buf, len, 16000)` 流式播放，SHALL NOT 把整个文件读入 RAM，
SHALL NOT 用阻塞式抽干循环长时间冻结主循环（clawd 动画 / BLE 更新回放期间 SHALL 继续）。
回放 SHALL 可被按键停止，播放到文件结束 SHALL 自动结束并回到列表。

#### Scenario: 回放选中笔记

- **WHEN** 列表中选中一个笔记并按 `enter`
- **THEN** 固件 SHALL 从头流式播放该 WAV（Speaker.playRaw，分块）
- **AND** 主循环（clawd/BLE）SHALL 在回放期间继续运行

#### Scenario: 停止与播完

- **WHEN** 回放中用户按停止键，或文件播放到结束
- **THEN** 固件 SHALL 停止播放、关闭文件、回到列表态

### Requirement: 回放与录音互斥

固件 SHALL 保证回放与录音不同时进行：录音进行中 SHALL NOT 启动回放，回放进行中 SHALL NOT
启动录音。回放仅使用 Speaker（正常态已 begin），SHALL NOT 触碰 Mic。

#### Scenario: 录音中不回放

- **WHEN** 正在录音时尝试进入回放
- **THEN** 固件 SHALL 忽略或提示，SHALL NOT 同时开 Mic 与 Speaker

### Requirement: 浏览/回放期间不熄屏

固件 SHALL 把「笔记列表打开」或「正在回放」计入 screen-off 的活动条件，使浏览列表或听回放
期间屏幕 SHALL NOT 因超时熄灭。

#### Scenario: 听回放时屏保持亮

- **WHEN** 正在回放且用户未按键、未移动设备超过熄屏阈值
- **THEN** 屏幕 SHALL 保持点亮（回放/列表态算作活动）

### Requirement: SD 读取失败安全降级

固件 SHALL 在列表扫描或回放读取失败（SD 未挂载、拔卡、读错误）时安全处理：提示或空列表、
停止回放并关闭文件，SHALL NOT 崩溃或卡死。

#### Scenario: 回放中拔卡

- **WHEN** 回放过程中 SD 读取失败
- **THEN** 固件 SHALL 停止回放、关闭文件、回到安全态，SHALL NOT 崩溃
