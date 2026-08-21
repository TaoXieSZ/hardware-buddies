# dsh-walkie

把 walkie-bridge 的编排层做成 DSH 插件:DSH 是大脑,StopWatch 手表是物理 I/O 与安全闸。

## 能力

- **操作面**(任意 DSH 会话):`walkie_status` / `walkie_events` / `walkie_propose` /
  `walkie_resolve` / `walkie_say` / `walkie_wait`
- **语音路由脑**:专职 headless 值班会话(标题 `walkie-duty`),长轮询 bridge 的
  transcript 队列,硬化 prompt → 结构化 JSON → decision;bridge 超时兑底确定性路由器。

## 安装(本机)

1. **bridge 侧**(`../walkie-bridge/.env`,重启 bridge 生效):

```bash
WALKIE_BRAIN_ENABLED=1
WALKIE_BRAIN_TOKEN=<openssl rand -hex 32>
WALKIE_BRAIN_WHITELIST_JSON='["^git\\s+(status|diff|log)\\b","^(查看|看看|查一下)","^tail\\s","^cat\\s"]'
```

2. **插件配置** `~/.config/walkie-bridge/brain.json`:

```json
{
  "base_url": "http://127.0.0.1:8767",
  "duty": { "enabled": true, "workspaceId": null, "backoffMs": 5000 }
}
```

token 优先级:环境变量 `WALKIE_BRAIN_TOKEN` > `brain.json.token` >
`brain.json.bridge_env` 指向的 .env 文件(默认读 `~/.config/walkie-bridge/bridge.env`)。

3. **注册**:在 `~/.dsh/profiles/web/cordis.patch.yml` 追加:

```yaml
- insert:
    - id: dsh-walkie
      name: /Users/taoxie/hardware-buddies-walkie/stopwatch-walkie/tools/walkie-dsh-plugin/index.js
```

4. **重启 dsh web**(host 插件不热加载)。

## 安全模型

- bridge 是唯一权威;brain API 只绑 127.0.0.1 + Bearer token。
- 大脑决策的目标必须过 bridge 的"唯一匹配 + steer 能力"校验。
- 免手表确认仅限 `WALKIE_BRAIN_WHITELIST_JSON` 正则命中(默认查询类);
  其余一律圆屏提案,手表 KEYA 批准 / KEYB 拒绝。
- 权限仲裁:手表与大脑 first-response-wins,cc-bridge 拒绝迟到应答。
- 值班会话的 prompt 把转写标注为不受信任数据(见 `lib.js` 的 buildRoutingPrompt),
  且转写内容中的尖括号被中和,无法越出数据区。

## 测试

```bash
node --test                 # lib.js 纯函数 12 例
cd .. && .venv/bin/python -m pytest tests/ -q   # bridge 86+1 例(含 brain API)
```

## curl 冒烟(bridge 重启后)

```bash
TOKEN=<同上 token>
curl -s -H "Authorization: Bearer $TOKEN" http://127.0.0.1:8767/api/v1/status | head
curl -s -H "Authorization: Bearer $TOKEN" 'http://127.0.0.1:8767/api/v1/brain/queue?wait_ms=1000'
```
