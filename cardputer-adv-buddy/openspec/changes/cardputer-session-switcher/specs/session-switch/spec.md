## ADDED Requirements

### Requirement: 选中会话触发 selectSession 命令

固件 SHALL 在会话列表里对当前选中的会话提供一个「确认」动作（键盘 `ok`/`enter`），按下时经 NUS RX 回送一条行分隔 JSON 命令 `{"cmd":"selectSession","sid":"<session_id>"}`，其中 `sid` 取自该会话在状态 payload `sessions[]` 中的标识。命令格式 SHALL 与既有审批回送（`{"cmd":"permission",...}`）保持同构（同一 NUS 通道、同样行分隔 JSON）。

固件发出命令后 SHALL NOT 在本机做任何窗口/详情切换——切换发生在 Mac 端真终端，cardputer 仅作选择器（见 Non-goals）。

#### Scenario: 选中并确认发出命令

- **WHEN** 会话列表打开、某会话被选中，用户按 `ok`/`enter`
- **THEN** 固件 SHALL 经 NUS RX 写出 `{"cmd":"selectSession","sid":"<该会话 sid>"}`
- **AND** 固件 SHALL NOT 在 cardputer 上打开该会话的 prompt 详情或审批面板

#### Scenario: 无连接时不发命令

- **WHEN** 会话列表打开但与 bridge 未连接（离线态）
- **THEN** 按确认键 SHALL NOT 崩溃，且 SHALL NOT 排队堆积命令（直接忽略或给一次性轻提示）

### Requirement: bridge 经 cmux 将对应终端切到前台

`cc-bridge` SHALL 在收到 `selectSession(sid)` 时，实时查询 cmux（`cmux rpc surface.list`），在返回的 surfaces 中线性匹配 `resume_binding.checkpoint_id == sid` 且 `resume_binding.kind == "claude"` 的 surface，取其引用调 `cmux focus-panel --panel <surface.ref> [--workspace <ws.ref> --window <win.ref>]`，把对应终端（含其 workspace tab 与 window）切到前台。

bridge SHALL NOT 自行维护 `session_id ↔ pane` 映射表——映射每次从 `surface.list` 实时读取，避免状态漂移（cmux 的 `checkpoint_id` 即 Claude `--session-id`，由 cmux 的 agent-hook 记录）。

#### Scenario: 命中并聚焦

- **WHEN** bridge 收到 `selectSession(sid)` 且 cmux `surface.list` 中存在 `checkpoint_id == sid` 的 claude surface
- **THEN** bridge SHALL 调 `focus-panel` 聚焦该 surface
- **AND** 该终端的 workspace 与 window SHALL 一并切到前台

#### Scenario: 实时查询而非缓存映射

- **WHEN** bridge 处理任一 `selectSession`
- **THEN** bridge SHALL 以当次 `surface.list` 结果为准定位目标
- **AND** SHALL NOT 依赖此前缓存的 sid→pane 映射

### Requirement: 查无匹配与调用失败的降级

bridge SHALL 在 `surface.list` 中找不到匹配 `sid` 的 surface（例如该 Claude session 由手动启动、未经 cmux agent-hook 记录），或 `focus-panel` 调用失败时，安全降级：记录一条日志并忽略本次选择，SHALL NOT 崩溃、SHALL NOT 误聚焦其它 surface。

#### Scenario: 无对应 surface

- **WHEN** `selectSession(sid)` 的 `sid` 在 cmux `surface.list` 中无 `checkpoint_id` 匹配
- **THEN** bridge SHALL 忽略本次切换并记录日志
- **AND** SHALL NOT 聚焦任何其它（非目标）surface

#### Scenario: cmux 调用失败

- **WHEN** `focus-panel` 返回非零或 cmux socket 不可达
- **THEN** bridge SHALL 捕获失败并继续正常服务（BLE 状态镜像/审批不受影响）

### Requirement: sid 与 Claude session_id 的契约

状态 payload `sessions[]` 中每个会话的 `sid` SHALL 等于该会话的 Claude `session_id`，以便与 cmux 的 `resume_binding.checkpoint_id` 直接相等匹配。bridge SHALL 保证该字段稳定（同一会话生命周期内不变），供固件选中后原样回传。

#### Scenario: sid 可被 cmux 直接匹配

- **WHEN** 某会话以 `sid=S` 出现在 payload，且其在 cmux 中对应 surface 的 `checkpoint_id=S`
- **THEN** 固件回传 `selectSession(S)` 后 bridge SHALL 能以 `checkpoint_id == S` 唯一定位该 surface
