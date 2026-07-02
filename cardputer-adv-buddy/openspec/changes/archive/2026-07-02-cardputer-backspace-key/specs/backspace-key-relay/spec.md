## ADDED Requirements

### Requirement: NORMAL 态物理 Backspace/Del 键回送退格

固件 SHALL 在 NORMAL 态（无审批层、无会话列表、无帮助层、无问答覆盖层时）监听物理
Backspace/Del 键（`M5Cardputer.Keyboard.keysState().backspace`——真机实测 Cardputer-ADV 该键置位的是
`backspace` 字段而非 upstream 官方示例 `inputText.ino` 用的 `.del`（标准版 Cardputer
键盘映射与 ADV 不同，见 design.md Decisions））。该键在 `isChange()` 边沿被置位时，固件 SHALL 经既有
`cclink::sendKeyName("backspace")` 通道回送一次 `{"cmd":"key","name":"backspace"}`
给 Mac 侧聚焦的 Claude 终端，并 SHALL 播放与其余 NUDGES 键一致的 toast（如
`sent: backspace`）+ 提示音反馈。

#### Scenario: 单次按下回送一次退格

- **WHEN** NORMAL 态下用户按下物理 Backspace/Del 键（`ks.backspace` 在本帧 `isChange()` 时为真）
- **THEN** 固件 SHALL 调用 `cclink::sendKeyName("backspace")` 恰好一次
- **AND** SHALL 显示 toast 提示已发送退格
- **AND** SHALL NOT 因该帧同时非空的 `ks.word` 而重复触发或漏触发

#### Scenario: 非 NORMAL 态不响应

- **WHEN** 审批层、会话列表、帮助层或问答覆盖层任一处于显示状态
- **THEN** 固件 SHALL NOT 因 `ks.backspace` 回送退格（该按键此时归属对应覆盖层的既有按键语义
  或被忽略，不与本需求冲突）

#### Scenario: 按住不连续重复

- **WHEN** 用户按住物理 Backspace/Del 键不放
- **THEN** 固件 SHALL 只在按下的 `isChange()` 边沿回送一次退格，SHALL NOT 每帧重复回送
