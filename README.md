# hardware-buddies

把分散的硬件 / 嵌入式桌面伴侣项目整合到同一个 monorepo。每个子目录是一个独立产品，通过 `git subtree`（**保留各自完整 git 历史**）从原仓库引入。

## 子项目

| 目录 | 产品 | 技术栈 | 原仓库 | 设备 |
|---|---|---|---|---|
| [`ahakey/`](./ahakey) | AhaKey Desktop —— AhaKey-X1 (Vibecoding) 键盘伴侣 | Swift + SwiftUI / Python | `github.com/TaoXieSZ/desktop` | BLE 键盘（OLED + LED + 拨杆审批） |
| [`claude-code-buddy/`](./claude-code-buddy) | Claude Code Desktop Buddy —— 桌面陪伴硬件 | ESP32 (PlatformIO) + 桌面 app | `github.com/TaoXieSZ/claude-code-buddy` | ESP32 / M5 Tab5 |
| [`m5-paper-buddy/`](./m5-paper-buddy) | M5Paper Buddy —— 墨水屏桌面伴侣（M5Stack 家族） | ESP32 (PlatformIO) | `github.com/op7418/m5-paper-buddy`（第三方上游） | M5Paper |

## 这些项目的共同点

都是「把 IDE / AI agent 的状态映射到一块实体硬件」的桌面伴侣：
- **AhaKey** —— 键盘的 LED 灯条反映 IDE 状态，物理拨杆作为工具审批的开关。
- **Claude Code Buddy** —— ESP32 屏幕角色随 Claude Code 会话状态变化。
- **M5Paper Buddy** —— 墨水屏上的伴侣形象。

## 历史与同步说明

- 每个子目录通过 `git subtree add --prefix=<dir> <repo> main` 引入，**完整保留原始提交历史**（`git log ahakey/` 可看到原 commit）。
- `ahakey/` 与 `claude-code-buddy/` 是本人仓库（TaoXieSZ），后续可双向同步。
- `m5-paper-buddy/` 来自第三方上游 `op7418/m5-paper-buddy`，是 fork 快照；如需跟上游更新用 `git subtree pull --prefix=m5-paper-buddy <upstream> main`。

## 引入新子项目

```bash
git subtree add --prefix=<目录名> <repo-url-or-local-path> <branch>
```

## 待评估（尚未纳入）

| 仓库 | 原因 |
|---|---|
| `clawd-on-desk` | 屏幕桌宠（Electron 软件，非实体硬件） |
| `kindle-dashboard` | Kindle 墨水屏 dashboard（第三方，server 渲染，非固件） |
| `buddy-voice` | Agora 对话式 AI web demo，非硬件 |

需要纳入任何一个，告诉我即可。
