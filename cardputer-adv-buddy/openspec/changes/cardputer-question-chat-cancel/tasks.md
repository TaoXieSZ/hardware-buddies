# Tasks

> 状态：proposal 草拟中（探索阶段产物），**未实现**。任务 0 是开工前的 gating spike，结果决定后续走主路还是 fallback。

## 0. Gating spike（已做，结论：主路）
- [x] 0.1 验证 `cmux feed.question.reply` 接受自由文本 — ✅ 接受（opencode-plugin.js:226-228 `selections.map(s=>[String(s)])` 零校验；replyQuestion 透传 POST /question/{id}/reply）
- [x] 0.2 结论：走主路 D1/D2/D3；fallback 不启用。附带：原生 `/question/{id}/reject` 存在但 cmux 未暴露成 rpc，cancel 暂用 reply+skip 文本
- [x] 0.3 文案敲定（canned MVP）：
  - chat = 「我想先聊聊这个，先别急着定 —— 能展开讲讲各选项吗？」
  - skip = 「先跳过，你按最佳判断继续。」
  - Fork A 定为 canned MVP（typed 列 task 3 后续，不在本轮）

## 1. 线协议：answerQuestion 加 text  ✅ 编译通过
- [x] 1.1 `cclink` 新增 `sendAnswerText(rid, text)` 回送 `{"cmd":"answerQuestion","rid":..,"text":..}`（转义 "/\，UTF-8 透传）— cclink.h:14 / cclink.cpp:189-209
- [x] 1.2 与既有 `sendAnswerQuestion(rid, ids)` 并存；二者互斥

## 2. 固件 UI：chat / cancel 两个 meta 选项  ✅ 编译通过
- [x] 2.1 屏底提示加 `c聊·esc跳`（clawd_player.cpp:280）
- [x] 2.2 `main.cpp` question 层按键：`c` → chat 应答；`` ` ``/esc → cancel 应答（回送 skip 文本，替换静默撤）— main.cpp:172-211
- [x] 2.3 chat/cancel 提交后撤面板 + 标记 `g_dismissedQRid`，与现有 dismiss 收尾一致

## 3.（secondary，可选）设备端 typed 自由输入
- [ ] 3.1 文本输入覆盖层：键盘逐字累积 UTF-8、退格、提交、取消
- [ ] 3.2 提交 → `sendAnswerText(rid, 用户文本)`；取消 → 返回选项视图

## 4. cc-bridge 桥接  ✅ pytest 184 passed（含新增 free-text 用例）
- [x] 4.1 收到带 `text` 的 `answerQuestion` → `feed.question.reply` 以 Other/自由文本提交（主路）— core.py on_stick_line 加 text 分支（回调签名 `(rid, ids, text)`）；bridge.py `_answer_question` text 直接当答案
- [~] 4.2 fallback（终端注入）不启用 —— spike 确认主路成立
- [x] 4.3 `rid` 失效 / reply 失败 → 记日志、忽略、不崩（沿用既有 try/except + answer_question fire-and-forget）
- [x] 4.4 同步线上 `claude-desktop-buddy` 仓：core.py + bridge.py + test_buddy_core.py 套同一 patch（目标区域与 monorepo 字节一致），live pytest 184 passed。⚠️ 跑着的 daemon 仍是旧码，真机验证前需 `launchctl kickstart -k gui/$(id -u)/com.cc-bridge` 重载

## 5. 验证
- [x] 5.1 真机：chat about it 一键 `c` → 自由文本回送 → 答案回灌（2026-06-24 烧录后真机验证）。daemon 日志铁证：
  `tx raw +180 {"cmd":"answerQuestion",…}` → `ids=None text=True` → `reply=True (free text)`；rid 含本会话 92a4ff24，chat 文案原样回到 AskUserQuestion
- [ ] 5.2 真机：cancel（esc）→ skip 文本解阻 —— 本轮只按了 `c` 验 chat；esc 路径同代码路径、未单独按键验证
- [x] 5.3 真机：选项选择路径回归未坏 —— 日志 `ids=['opt2'] text=False → reply=True selections=[…]`，option 路径照常
- [ ] 5.4 （若做 typed）真机：打字 → Other 自定义答案回送正确 —— typed 未实现（task 3 后续）
- 旁注：pytest 184 passed（monorepo + 线上仓）；firmware 编译过；烧录走 ROM 下载模式（原生 USB 跑着固件时直烧会 No serial data received）
