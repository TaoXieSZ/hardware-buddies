# 产品思考与畅想

> 为什么要做一块专门看 Claude Code 的物理墨水屏？它解决了什么？未来会变成什么？
> 本文面向想理解「这东西设计上在想什么」的人，以及想给它提产品方向的人。

---

## 1. 起点：Claude Code 没有"外部器官"

跑 Claude Code 的时候，信息其实分成两类：

1. **交互信息**（你要下判断的东西）：
   - Claude 想执行一个 bash 命令，你要不要同意？
   - 它问了你一个问题，选哪个方案？
2. **伴随信息**（你偶尔扫一眼就够的背景）：
   - 现在跑到哪了？上下文还剩多少？
   - 在哪个分支？dirty 文件有几个？
   - 这会儿我开了几个窗口？

两种信息都挤在终端里：

- **交互信息**打断你当前在做的事（终端弹审批框、等你按回车）
- **伴随信息**要主动滚屏 / 切窗口 / `git status` 才能看到 —— 经常懒得看

终端是**主输入法**，但它不擅长当「状态面板」。当你有 3-4 个 Claude Code 窗口同时跑的时候，你其实在脑子里 maintain 一张表 —— 每个窗口在哪个项目、是不是在等你、是不是快超预算了 —— 但你根本看不到这张表。

**m5-paper-buddy 的起点**：把上面这张表搬到一个**物理的、永远亮着的、不抢焦点的**屏上。

---

## 2. 为什么选墨水屏 + 物理按键

### 墨水屏的性格适合做「陪伴」

- **永远亮着** —— 电子墨水断电保字，不耗电也不刺眼。可以摆桌上一天不管
- **不闪不抢** —— LCD 有颜色、有动画、有刷新率，墨水屏不抢你主屏的注意力
- **书面感** —— 字很"静"，像翻开一本便签本。适合展示需要**被阅读**的东西
- **缓慢更新是 feature** —— 我们故意把空闲 dashboard 的刷新放到 30 秒一次。这是**特性**不是缺陷：你不需要看到它每一秒钟的抖动

### 物理按钮让「审批」变成有仪式感的动作

terminal 里按 `y` 和按物理按键，手感上完全不一样。

- 终端按 `y`：手指在键盘上、视线切到弹窗、决策权重低
- 物理 PUSH：手伸出去、按下去有力反馈、**确认了某个动作真的发生了**

这对 agentic tool 特别关键 —— 尤其是 `rm -rf` / `git push --force` / 改外网 API 这种不可逆操作。物理按钮给决策一点**物理成本**，防你手滑。

### 触摸屏补上"多选一"这种不适合按钮的场景

三个硬件按钮做 approve / deny / refresh 够用，但遇到 `AskUserQuestion` 四选一就不够了。GT911 电容触屏刚好补齐：大按钮直接点。

---

## 3. 核心交互

### 3.1 多会话 dashboard：把脑内表格搬到桌上

**之前**：
```
tmux ls
attach to claude-0
# 啊它还在 running，切回去
tmux a -t claude-1
# 这个在等我……
```

**现在**（抬头看 Paper）：
```
SESSIONS            MODEL
* m5-paper-buddy    Opus 4.7
* claude-to-im      上下文
! Generative-UI-MCP 45.8K / 200K
                    ▓░░░░░
```

`*` = running · `!` = 等你审批 · `.` = idle。一眼看出哪个该关注。

**点一下换 focus** —— 右边的 model / 上下文 / 最近回复 / 活动日志切过去。像刷朋友圈，手指点两下。

设计原则：**状态切换不打扰审批流程**。你点 session 只影响 dashboard 看什么，审批照样按 FIFO 一条条弹，不会因为你切了 focus 就漏审某一条。

### 3.2 审批：从「弹窗打断」到「物理确认」

**之前**：
- 终端弹一行 `Allow execution of: rm -rf /tmp/foo? (y/n)`
- 你瞟一眼 → 按 y → 继续
- 事后想：刚才那条是啥来着？

**现在**：
- Paper 全屏弹审批卡（字大）
- 完整显示：tool 名 + 项目 + 实际内容（bash 命令原文 / edit 的 diff / write 的文件+预览）
- **你必须物理走一步**：按 PUSH 同意 or DOWN 拒绝
- 审批记录在活动日志里留痕

**DND 模式**是对"懒得审批"的安全降级：长按 UP 切开，所有后续 PreToolUse 在 0.6 秒内自动 approve。但徽章上一直亮着 "DND"，你一眼就知道现在是**放行模式**。这比"关掉 permission check"更自觉。

### 3.3 触屏回答：从"换焦点敲数字"到"直接点"

`AskUserQuestion` 场景 —— Claude 给你 4 个方案让你选。终端里要 tab 或 1/2/3/4。Paper 上四个大卡片，手指点一下 → 日志里多一行 `AskUserQuestion → React`，Claude 那头看到 "user answered React, do NOT ask again" 直接用。

### 3.4 语言切换：CJK 不是"支持一下"，是一等公民

大多数开发者工具对 CJK 的态度是"能显示就行"。但对一个放在桌上的伴侣屏，中文内容被截断或显示成问号是**硬伤**。所以：

- 3.4MB TTF 占 LittleFS 大头（特地把分区表改了）
- `wrapText` 专门写了 codepoint-aware 的版本（不是按字节切）
- UI 标签全走 `LX("English", "中文")` 宏
- 切换立即触发 GC16 全刷，避免两种字体在墨水屏上重叠出鬼影

---

## 4. 目标用户

按"想不想要"的强弱：

1. **Claude Code 重度用户** —— 每天开 3+ 窗口，常见多项目切换。这群人会立刻理解为啥需要 dashboard
2. **介意安全的开发者** —— 愿意多按一下按钮换可控感。给 Claude 开权限的心理成本降低
3. **Maker / 硬件玩家** —— 把终端用具实体化本身就是好玩的事；M5Paper 这种小设备摆桌上有"桌面陪伴"的情绪价值
4. **内容创作者 / 开源维护者** —— 有一块屏展示自己正在 Claude 里干啥，适合直播 / 教学 / 记录

---

## 5. 对比 Anthropic 的 claude-desktop-buddy

这个项目的**起点**是 `anthropics/claude-desktop-buddy`（Felix Rieseberg 的作品），但两者定位很不一样：

| | claude-desktop-buddy (原项目) | m5-paper-buddy (本项目) |
| --- | --- | --- |
| **目标硬件** | M5StickC Plus（135×240 彩色 TFT） | M5Paper V1.1（540×960 电子墨水屏） |
| **驱动方式** | Claude **桌面版** 内置 BLE bridge | 独立 daemon + Claude **Code** hooks |
| **审批** | 按 A / B 两键 | PUSH / DOWN + 触屏 |
| **多会话** | 只看数 | 多 session 列表 + 可切 focus |
| **AskUserQuestion** | 不支持 | 4 个大触屏选项 |
| **语言** | 英文 ASCII | 英 / 中双语 UI + CJK 字体 |
| **伴侣形态** | tamagotchi 电子宠物（18 种 ASCII + GIF 动画）| 信息面板 +简化版猫 |
| **取向** | 玩具、好玩、tamagotchi 系 | 工具、看板、生产力向 |
| **Transport** | BLE 专属 | USB + BLE 自动切 |

**上游是个好玩的玩具**，我们在同一套协议下做了**一件具体工作工具**。两个方向都对，只是场景不同。

---

## 6. 未来畅想

### 短期（几个 PR 内能做）

- **更好的 buddy** —— 现在的猫是从上游搬的 ASCII。可以做个全新的静帧 + 偶尔变换的插画，更适合墨水屏的质感
- **更多工具的 body 模板** —— WebSearch 结果预览、NotebookEdit 的 cell diff、TodoWrite 的任务列表
- **主动通知** —— session 空闲超过 N 分钟发一下提示（"你的 Claude 等你回复 10 分钟了"），或者审批超时前预警
- **夜间/白天双主题** —— 墨水屏可以做反色，晚上看更柔
- **更细的活动过滤** —— 按 session 过滤 ACTIVITY，不同项目的日志分开

### 中期（更大的工程）

- **WiFi 无线模式** —— daemon 不 bind 在 USB / BLE，在局域网跑 HTTP 服务；Paper 通过 WiFi 连。桌子上三台 Paper 都能接同一个 daemon
- **多设备阵列** —— 一台 daemon 同时驱动多块 Paper，比如桌面主屏 + 键盘旁边小屏 + 墙上 24/7 公共屏
- **会话复盘视图** —— 把一天所有的 session 活动攒起来，做成"今日复盘"页。触屏滑动翻页
- **GitHub 状态融合** —— 当前分支的 PR / CI / Review 状态一起显示
- **Cost / Budget 综合视图** —— 不只上下文，还有 API 费用累积（如果 Claude API 那边给 usage endpoint）
- **Token 流式进度** —— 看着 Claude 正在打字一样，进度条缓缓涨

### 长期（方向性畅想）

- **通用 "agent buddy" 协议** —— 把 Nordic UART + heartbeat JSON 的 schema 开源出来，让它不是 Claude 专属，其它 agent framework（OpenAI's Assistants, local LLM agents, Cursor agent 等）也可以接入同一块硬件
- **专属 SKU** —— 现在要你自己买 M5Paper + 烧录。可以做成一个预烧好、带 Custom PCB、带专属外壳的成品，插上就用
- **桌面仪式** —— 把这块屏变成"工作会话的开始和结束"的物理标记。早上点一下开始，晚上点一下结束，帮你切回生活状态
- **团队共享审批** —— 一块 Paper 放办公室里，任何团队成员的 Claude 操作都会在上面弹审批。谁最近按 PUSH，由谁背锅（笑）。适合高风险基础设施场景
- **AI 伴侣的人格锚** —— 现在 agent 没有"身体"，它在哪里都是你键盘里的 ghost。一块专属的物理屏就是给它**一个家**。未来的 agent 个性化可以用硬件做载体 —— 不同设备不同 "personality"

### 留给硬件生态的机会

Claude / OpenAI / Anthropic 主流不会做硬件。留给我们的是：

- **配件经济** —— 像机械键盘 / 显示器支架 / 耳机一样，围绕 AI 编码形成的专属桌面配件
- **Agent 可观测性硬件** —— 专门服务于 agent 透明度（能看到 agent 在干啥、给你"否决权"）的设备类别
- **开源硬件 + 开源协议** —— 如果协议公开、固件开源，这个类别可以有大量变体百花齐放

---

## 7. 设计不妥协的几个点

写的过程中有几次我们差点走妥协路线，最后没走。留个备忘：

1. **中文一定要原生显示**，不是降级成拼音或问号 —— 所以才顶着 3.4MB 字体 + 重写 wrap 逻辑
2. **审批卡要显示完整内容**，不能只给工具名 —— 硬件审批的唯一相对桌面弹窗的优势就是**你能看到要批什么**
3. **多会话要能物理切换**，不是被动显示最新一个 —— 否则和 `tmux a -t` 无异
4. **墨水屏不做动画** —— 闪烁是反墨水屏美学的。接受刷新慢是它的一部分
5. **USB 模式要零配置** —— 插上就能用，先让价值被看到，再谈 BLE 无线
6. **协议向后兼容** —— 所有新字段都是可选的，老固件见到就忽略，避免一换 daemon 版本就要重烧固件

---

## 8. 叫人上手，不是做给极客看

虽然本仓库 technical 得离谱（要会 PlatformIO、懂 BLE、懂 Claude Code hooks），但最终**面向用户的接口**是：

```
/buddy-install
/buddy-start
```

两条命令。其它全藏在 install.sh 里。能自动装 PlatformIO / Python 依赖 / mklittlefs 架构修复 / hooks 合并 / 刷固件 / 起 daemon 的，就别让用户手动操作。

---

## 9. 给想 fork 的人

如果你想：

- **只改 UI** —— 动 `src/paper/main.cpp` 的 draw* 函数和 TS_* 尺寸常量
- **换套伴侣形象** —— 改 `src/paper/buddy_frames.h`，或者用位图（参考 M5EPD_TTF 示例加载 PNG）
- **支持别的 agent framework** —— 保留 heartbeat JSON schema，重写 `tools/claude_code_bridge.py`，换 hook 源
- **换一块硬件（不是 M5Paper）** —— 见 `docs/ARCHITECTURE.md` 第 8 节"怎么扩展"

按 GPL-3.0 + 署名条款，欢迎魔改；请把你改出的版本也开源回社区。

---

## 相关

- 技术细节：[docs/ARCHITECTURE.md](ARCHITECTURE.md)
- 上游参考：[anthropics/claude-desktop-buddy](https://github.com/anthropics/claude-desktop-buddy)
- 安装使用：[README.md](../README.md)
