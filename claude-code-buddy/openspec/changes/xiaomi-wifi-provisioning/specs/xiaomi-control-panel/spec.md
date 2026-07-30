# xiaomi-control-panel Spec Delta

## ADDED Requirements

### Requirement: 网络管理 API
控制面板 SHALL 提供网络管理接口：列出已存网络（`GET /api/networks`）、添加或更新
（`POST /api/networks`）、删除（`DELETE /api/networks`）、扫描附近网络
（`GET /api/networks/scan`）。列表响应 MUST NOT 包含任何密码字段。扫描为阻塞操作，
MUST 在 HTTP 任务中执行，不得影响主循环与音频链路。

#### Scenario: 列表不含密码
- **WHEN** 调用 `GET /api/networks`
- **THEN** 返回 SSID 列表与当前连接标记，响应体中不出现任何密码

#### Scenario: 扫描不影响对话
- **WHEN** 对话进行中用户在面板触发附近网络扫描
- **THEN** 该轮对话 underrun 仍为 0，扫描结果正常返回
