# Cardputer 监听多 agent（Claude / Cursor / Codex）— 架构分析

## 一句话结论

**不用从头重构,固件/线协议已经是 agent-无关的;要改的是 daemon —— 从「单 agent hook→状态」演进成「按 cmux 会话聚合多源状态」的 aggregator。** 复杂度主要看你要给非 Claude agent 多丰富的状态。

---

## 当前架构（Claude-only）

```
  Claude Code hooks ─┐
   (thinking/tool/    │   每事件带 session_id
    waiting,丰富)     ▼
                   ┌──────────┐   BuddyState        BLE
  cmux feed ──────►│ cc-bridge │──(sessions[]:      (Claude-7AFD)
   (questions,     │  daemon   │   sid,label,st,ws)────────────► cardputer
    labels;按      └──────────┘   ← 我们刚加的                  (渲染 sessions[]:
    checkpoint_id)                  per-session 状态              轮播/钉/标识)

  Cursor ──► cursor-bridge ──BLE(Cursor-*)──► 另一块设备（平行，不合并）
  Codex  ──► (没有集成)
```

**约束:一个 BLE 设备只能被一个 central 占用** → 一块 cardputer 现在只连一个 bridge（=只看一个 agent 的会话）。cc-bridge 和 cursor-bridge 是**两个平行 daemon、两个 BLE 前缀**,各管各的。

---

## 关键拆解:登记处 vs 状态来源

| 维度 | 谁提供 | 是否 agent-无关 |
|---|---|---|
| **会话登记**（有哪些 pane、名字、焦点） | **cmux**（所有 pane,任何 agent） | ✅ 本来就无关 |
| **per-session 语义状态**（thinking/tool/waiting） | **各 agent 自己的 hook**（Claude→cc-bridge;Cursor→cursor-bridge;Codex→无） | ❌ 每 agent 单独 |
| **线协议 + 固件渲染**（sessions[]{sid,label,st,ws}） | 我们刚做的 | ✅ 不关心是什么 agent |

**核心洞察**:cmux 给你「有哪些会话」是免费且跨 agent 的;难的是「每个会话在干嘛」—— 这需要**每个 agent 各自的状态源**。

---

## 提案架构（一块 cardputer 看全部 agent）

```
                  ┌─ Claude hooks ───────► 丰富状态(thinking/tool/waiting)
   cmux ──登记──┐ ├─ Cursor hooks ───────► 丰富状态（cursor-bridge 现成逻辑折进来）
  (所有 pane:   │ └─ Codex / 通用 pane-活性► 粗状态(busy/idle，看进程/输出变化)
   nickname,    │                              │
   surface,     ▼                              ▼
   focused,   ┌─────────────────────────────────────┐
   title)     │   AGGREGATOR daemon                  │
              │   • 按 cmux 会话(surface)归一        │
              │   • 统一 BuddyState.sessions[]       │
              │     每条: {sid,label,st,ws,agent?}   │
              │   • 单一 BLE owner（不是 3 个 bridge │
              │     抢链路）                          │
              └───────────────┬─────────────────────┘
                              │ BLE
                              ▼
                    cardputer（固件**不用改** —— 已 agent-无关）
```

---

## 复杂度逐项（要不要改、改多大）

| 部分 | 要改吗 | 工作量 | 说明 |
|---|---|---|---|
| **固件 / 线协议** | ❌ 不改 | 0 | sessions[]{sid,label,st,ws} 不关心 agent;轮播/钉/标识照跑 |
| **会话列表覆盖全 agent** | 🟡 daemon | 低 | daemon 从「只列 claude 的 checkpoint」改成「列 cmux 所有 pane」 |
| **状态: Claude** | ✅ 已完成 | - | hooks → cc-bridge（含刚做的 per-session） |
| **状态: Cursor** | 🟡 折入 aggregator | 中 | cursor-bridge 逻辑已存在,挪进聚合、共用一个 BLE owner |
| **状态: Codex** | 🔴 新增 | 中-高 | Codex CLI 有 hook 吗?有→照 Claude 做;没有→退而求其次用 cmux pane 活性(进程在跑/输出在变=busy)给个粗状态 |
| **BLE 单 owner / 聚合** | 🟡 重构 daemon | 中 | 把「3 个平行 bridge」收敛成「1 个 aggregator + N 个状态源插件」 |

---

## 会不会太复杂?

**不会到「推倒重来」**:
- 你**最该庆幸的**:这次做的 session-rotation 已经把线协议/固件变成 per-session、agent-无关的了 —— 等于提前为多 agent 铺了路。固件一行不用动。
- **真正的活在 daemon**:把单 agent 的 cc-bridge 演进成「cmux 登记 + 多状态源 + 单 BLE owner」的 aggregator。
- **可分期**:
  1. 先让会话**列表/标识**覆盖全 agent（cmux 全 pane）—— 低成本,先看到 Cursor/Codex 会话出现在轮播里（哪怕状态先粗）。
  2. Cursor 状态折进来（cursor-bridge 现成）。
  3. Codex 状态:看它有没有 hook;没有就用 pane 活性给 busy/idle 粗态。

**最大的设计决断**:非 Claude agent 要**丰富状态**(thinking/tool/waiting)还是**粗状态**(busy/idle)够用?
- 粗状态够 → 中等工作量,cmux pane 活性通吃,不依赖每 agent hook。
- 要丰富 → 每个 agent 都得有自己的 hook 集成（Codex 是最大未知数）。

---

## 待核实的未知

- Codex CLI 有没有类似 Claude 的 hook 机制?（决定 Codex 能不能拿丰富状态）
- cursor-bridge 当前是独立 BLE 前缀;折进 aggregator 要解决「单 central」与状态合流。
- cmux 能不能查到非 claude pane 的「进程在不在跑 / 输出最近变没变」(给粗状态用)。
