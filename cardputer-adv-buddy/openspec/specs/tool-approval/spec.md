# tool-approval Specification

## Purpose
TBD - created by archiving change cardputer-claude-buddy. Update Purpose after archive.
## Requirements
### Requirement: 显示待审批的工具与参数

当 bridge 推送的状态置 `waiting`（有工具调用等待审批）时，固件 SHALL 在屏上弹出审批界面，显示待审批的工具名与参数（来自状态的 `prompt` 文本），使用户**看得见在批什么**。审批界面 SHALL 不永久遮挡 clawd（可缩为角标），且文本过长时 SHALL 截断或滚动而非溢出。

#### Scenario: 工具等待审批时弹出内容

- **WHEN** 收到 `waiting` 置位且带 `prompt`（如 "Bash: terraform apply"）的状态
- **THEN** 屏上 SHALL 显示该工具+参数文本与可选项提示（approve / deny / always）

#### Scenario: 长命令不溢出

- **WHEN** `prompt` 文本超过一屏宽度
- **THEN** 固件 SHALL 截断或滚动显示，不破坏界面布局

### Requirement: 键盘给出审批决定并回送

固件 SHALL 用键盘表达审批决定并经 NUS 回送给 bridge：`ok`(enter) → approve(once)，`esc` → deny，`a` → always(放行该工具)。回送格式 SHALL 与 bridge 期望的决定协议一致（once/always/deny）。决定送出后 SHALL 关闭审批界面、回到状态层。

#### Scenario: 按 ok 放行一次

- **WHEN** 审批界面打开且用户按 `ok`
- **THEN** 固件 SHALL 经 NUS 回送 "approve once" 决定
- **AND** 审批界面 SHALL 关闭，clawd 回到当前会话状态

#### Scenario: 按 esc 拒绝

- **WHEN** 审批界面打开且用户按 `esc`
- **THEN** 固件 SHALL 经 NUS 回送 "deny" 决定并关闭界面

#### Scenario: 按 a 永久放行该工具

- **WHEN** 审批界面打开且用户按 `a`
- **THEN** 固件 SHALL 经 NUS 回送 "always" 决定并关闭界面

### Requirement: 超时回落 ask

固件 SHALL NOT 强制用户必须在设备上决定。若审批界面打开后用户在超时窗口内未按键，固件 SHALL 让该决定回落为 ask（即不发 approve/deny，交回 Claude Code 终端原生提示），与 bridge 的超时语义一致。

#### Scenario: 不操作则交回终端

- **WHEN** 审批界面打开但超时窗口内无按键
- **THEN** 固件 SHALL 不发送 approve/deny（回落 ask），审批界面 SHALL 关闭

