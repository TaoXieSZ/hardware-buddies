# tasks.md — cardputer-unlabeled-sessions

## 1. 单测先行

- [x] 1.1 在 `tests/` 找到现有 `to_payload` sessions 构建的测试文件（参考 `tests/` 下 buddy_core 相关用例），新增用例：labels 1 条 + `_sessions` 2 条无标签 → `sessions[]` 共 3 条、顺序标签在前、无标签条目无 `label` 键
- [x] 1.2 新增用例：labels 16 条 → 无标签会话全被丢弃，总数 16
- [x] 1.3 新增用例：labels 14 + 无标签 5 → 总数 16，无标签取前 2
- [x] 1.4 回归用例：纯标签会话（无多余 `_sessions`）输出与变更前一致；`session_labels` 为空时回退分支不变
- [x] 1.5 确认 1.1–1.3 在未改实现时失败（红）

## 2. 实现

- [x] 2.1 修改 `tools/buddy_core/core.py` `to_payload` labels 分支：break 之后遍历 `_sessions` 追加无标签会话（跳过已在 labels 的 sid），复用 16 上限
- [x] 2.2 `make test-py`（须用 `~/.cc-bridge/venv/bin/python3` 环境的 pytest）全绿

## 3. 真机验证

- [x] 3.1 `launchctl kickstart -k gui/$(id -u)/com.cc-bridge` 重启 daemon，确认日志无异常、BLE 重连 `Claude-7AFD`
- [x] 3.2 真机按 `tab`：Claude 会话（带 label）在列表上部，Kimi 会话（sid 前缀显示）在下方
- [x] 3.3 确认 `total`/`running` 计数与变更前一致（无回归）
