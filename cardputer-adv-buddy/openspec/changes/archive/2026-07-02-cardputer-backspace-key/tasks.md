## 1. 实测键位分支

- [x] 1.1 在 `loop()` NORMAL 态分支新增 `Serial.printf("[del-key] del=%d backspace=%d\n",
      ks.del, ks.backspace)` 诊断打印，与实际接线（`if (ks.del) ...`）一起下发，
      真机 flash 后按物理 Backspace/Del 键观察串口输出，确认走 `del` 分支——
      **待真机验证**（见任务 3.1，需用户 flash 后确认输出与行为）。

## 2. 接线实现

- [x] 2.1 在 `src/main.cpp` NORMAL 态按键分发块（`!snapApproval && !snapSessions &&
      !snapHelp && !snapQuestion` 分支内，紧邻 `for (auto c : ks.word)` 循环处）新增
      `if (ks.del) { cclink::sendKeyName("backspace"); clawd::setToast("sent: backspace");
      sound::play("nudge"); }`，字段用 `ks.del`（design.md 的 Decision）。
- [x] 2.2 `pio run -e cardputer-adv` 编译通过（RAM 25.3%, Flash 43.9%，无警告）。

## 3. 真机验证

- [ ] 3.1 flash 到真机，连上一个真实 Claude 会话终端；在终端里打几个字符，按 cardputer
      物理 Backspace/Del 键，确认终端里对应字符被删除、cardputer 上出现
      `sent: backspace` toast + 提示音。
- [ ] 3.2 验证审批层 / 会话列表 / 帮助层 / 问答覆盖层显示时按同一个物理键，确认不触发
      退格回送（按对应覆盖层既有语义或被忽略）。
- [ ] 3.3 验证连续快速多次按键，每次按下都各回送一次退格，不漏发也不重复（按住不放
      只发一次，对齐 Non-goals 的「不做连续重复」）。

## 4. 收尾

- [x] 4.1 任务 1.1 真机实测发现字段与设计假设不符（该键实际置位 `ks.backspace` 而非
      `ks.del`）——已更新 `design.md` Decisions、`specs/backspace-key-relay/spec.md` 字段
      引用、`src/main.cpp` 实现，并把这条「upstream 示例跨板卡变体可能失效」的教训记入
      仓库根 `CLAUDE.md` 与本项目 `README.md`。
- [x] 4.2 `git status --porcelain` 确认改动只落在 `cardputer-adv-buddy/src/main.cpp` +
      本 change 的 openspec 目录，不触碰 `claude-code-buddy/` 或仓库根目录，
      与另一会话正在进行的 Tab5 开发无文件交集。
