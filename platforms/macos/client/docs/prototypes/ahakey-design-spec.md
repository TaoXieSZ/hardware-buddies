# AhaKey Studio · 设计规范（与 `ahakey-ux-review.html` 对齐）

本文档从当前 HTML 原型中抽取**可复用的视觉与结构约定**，便于 Web 迭代、设计稿标注，以及向 macOS / SwiftUI 迁移时对照。美学取向：**系统感（Apple iOS 语义色与层级）** + **Typeless 式桌面工具**（顶栏品牌、胶囊主按钮、侧栏导航与试用卡）。

---

## 1. 设计原则

| 原则 | 说明 |
|------|------|
| **单一事实来源** | 产品名、试用状态等「全局身份」放在**顶栏左侧**；设备连接、模式、电量等放在**顶栏中部**；列表侧栏只做导航与账号区，避免与顶栏重复。 |
| **层级用色不用阴影** | 工作台主壳以 `bg-card` + 细 `border` 分区；少用重阴影，与系统设置类 App 一致。 |
| **语义优先于实现** | 配置文案面向「场景 / 软件 / 批准方式」，Inspector 内避免固件字段直出。 |
| **深浅色一等公民** | 所有界面色尽量来自 CSS 变量；支持 `data-theme="dark" | light"` 与 `prefers-color-scheme` 自动。 |

---

## 2. 色彩体系（Design Tokens）

### 2.1 背景层级（由底到顶）

| Token | 浅色参考 | 用途 |
|--------|-----------|------|
| `--bg-base` | `#f2f2f7` | 应用窗口外沿 / 画布外氛围（类似 systemGroupedBackground） |
| `--bg-card` | `#ffffff` | 顶栏、工作台容器、画布/Inspector 卡片表面 |
| `--bg-control` | `#f2f2f7` | 次级表面：徽标底、侧栏图标底、Inspector 内 Panel、摘要卡 |
| `--bg-hover` | `#e9e9ef` | 可点击行、图标按钮、药丸按钮的悬停 |

深色下：`--bg-base` → `#000000`，`--bg-card` → `#1c1c1e`，`--bg-control` → `#2c2c2e`，`--bg-hover` → `#3a3a3c`。

### 2.2 文本层级

| Token | 用途 |
|--------|------|
| `--text-1` | 主标题、选中导航、强调数字 |
| `--text-2` | 正文、默认导航、状态行主文案 |
| `--text-3` | 辅助说明、时间戳感信息、分隔状态里的标签 |

### 2.3 边框与强调

| Token | 用途 |
|--------|------|
| `--border` / `--border-hover` | 1px 分隔线、控件描边；悬停略加深 |
| `--primary` | `#0a84ff`（`--ios-blue`），主行动、试用徽章描边混合 |
| `--primary-hover` | `#007aff`，主按钮悬停 |
| `--success` | `#34c759`（`--ios-green`），已连接指示等 |

### 2.4 遗留 / 局部语义（原型内仍可能出现）

以下在部分向导或旧块中使用，新功能**优先用上方 token**：

- `--brand-red`、`--working-blue`、`--warning`：硬件灯效说明、强调状态。
- `--muted`、`--text`：旧向导文案；新 UI 建议迁移到 `--text-2` / `--text-3`。

---

## 3. 字号与字重（Type Scale）

与原型中 `--fs-*`、`--fw-*` 一致，对标 **iOS 紧凑桌面工具**（非大标题营销页）。

| Token | 尺寸 | 典型用途 |
|--------|------|-----------|
| `--fs-large-title` | 22px | 极少用；大窗格标题预留 |
| `--fs-title1` | 20px | 预留 |
| `--fs-title2` | 17px | 画布区主标题（如「1:1 数字孪生画布」） |
| `--fs-title3` | 15px | 顶栏产品名、侧栏标题感文字 |
| `--fs-headline` / `--fs-body` | 15px | 与 title3 同级正文强调 |
| `--fs-callout` | 14px | 面板说明、向导段落 |
| `--fs-subhead` | 13px | 顶栏状态行、导航标签、药丸按钮 |
| `--fs-footnote` | 12px | 副标题、meta 徽章、面板脚注 |
| `--fs-caption1` | 11px | Pro Trial 小徽章、摘要卡小标题 |

字重：`--fw-semibold`（600）用于标题与选中项；`--fw-medium`（500）用于导航默认与说明。

**层级建议**：同一屏面上，**标题档差 ≥ 2px**（例如 17 vs 13/12），避免 15/14 混用导致糊在一起。

---

## 4. 圆角与间距

| Token | 值 | 用途 |
|--------|-----|------|
| `--r-sm` | 8px | 图标按钮、小图标容器 |
| `--r-md` | 10px | 卡片内 Panel、摘要卡、试用卡 |
| `--r-lg` | 12px | 顶栏/工作台外框、画布/Inspector 外卡片 |

**胶囊**：筛选、主/次行动、meta 信息条使用 **pill（`border-radius: 999px`）**。

**间距习惯**（与原型一致即可复用）：

- **三带纵向间距（顶栏 / 主工作区 / 底栏）**：**5px**，由 **`--shell-gap`** 控制，对应 `.app-shell` 的 CSS `gap`（仅分隔上、中、下三块，不含块内边距）。
- **应用外沿留白**：桌面约 `32px`（`padding`），窄屏约 `16px`；与 `--shell-gap` 独立。
- 顶栏内边距：约 `10px 12px`。
- Inspector 滚动区内边距：约 `18px`，块间距 `14px`。
- 顶栏三列网格：`auto 1fr auto`（品牌 | 状态 | 操作）。

---

## 5. 信息架构与布局

### 5.1 页面骨架

```
┌─────────────────────────────────────────────────────────┐
│ Toolbar：品牌（左） │ 设备状态（中） │ 主题/设置（右）      │
├──────────┬──────────────────────────────────────────────┤
│ Sidebar  │ Main：设备画布（约 65%）│ Inspector（约 35%） │
│ 导航      │              │ 滚动配置区                    │
│          │              │ 同步提示条（固定于滚动区与底栏之间）│
│          │              │ 底栏：状态徽标 + 次按钮 + 主按钮 │
├──────────┴──────────────────────────────────────────────┤
│ Status bar：运行指标（左） │ 新手引导等（右）                │
└─────────────────────────────────────────────────────────┘
```

### 5.2 职责划分

- **顶栏品牌区（`toolbar-brand`）**：Logo 字标 + 产品名 + 全局试用徽章（如 Pro Trial）。
- **顶栏状态区**：连接、电量、当前 Mode、AI 引擎等**随会话变化**的数据。
- **侧栏**：仅导航与底部账号 / 试用推广 / Dock 快捷。
- **画布顶条**：当前选中部位 + 当前 Mode 摘要（与顶栏 Mode 互补：顶栏短、画布可长句）。
- **Inspector**：可滚动配置；**底部同步提示条**（`inspector-sync-hint`）固定在滚动区与底栏之间，说明「修改配置文件后需通过同步到键盘生效」等文案，左侧为可点击图标（聚焦主按钮）；**主行动「同步到键盘」**固定于 Inspector 底栏右侧，**文案左侧配线框图标**（`.tl-pill-ic`，与 `.sys-ic` 同笔画规则）；**「切换模式」**为次行动，与同步状态徽标同组靠左。当状态为「已就绪」时，**隐藏底栏 meta 徽章**，由提示条承担说明，避免与「待同步」等徽章重复。
- **设备信息 / AI 引擎（侧栏切换）**：左侧仍为 **1:1 数字孪生画布预览**（**不展示**映射部位 / 工作模式控件，热点不可点）；与右侧 **设备信息** 或 **AI** 面板构成 **`1fr : 1fr` 双列**（相对配置台 `65fr : 35fr` 收窄左列，形成「预览」尺度；窄屏仍单列堆叠）。右栏 **`#viewDeviceInfo` / `#viewAi`** 与 **`#workbenchInspector`** 同槽显隐；模块圆角 **`--r-lg`** 与画布外卡片同级，内 **`panel` / `panel--remark`** 与 Inspector **§6.7 / §6.12** 一致。切换动效：**键盘区域不再做独立位移动画**，仅靠 **`grid-template-columns`** 与 **`--view-switch-ms`（约 400ms）** 的 **`--ease-product`** 过渡，避免动画结束态与布局复位「打架」产生回弹；右栏仅 **`workbench-pane-soft-in` / `soft-in-reverse`**（约 **5px** 平移 + **opacity 0.97→1**）。**`prefers-reduced-motion: reduce`** 时关闭上述动画。过渡期 **`overflow: hidden`** 防裁切穿帮。**预览列**下孪生键区 **`.keycap` / `.key-label` / `.oled-screen`** 字号随 **`data-view`** 略收，与列变窄同节奏过渡；**配置台**下映射行在 **`.canvas-card` `container-type: inline-size`** 的 **`@container twin-canvas (max-width: 640px)`** 内同步缩小图标与文案。

---

## 6. 组件规范

### 6.1 顶栏品牌（`toolbar-brand`）

- 布局：`space-between`，左组「标 + 名」，右 **Pro Trial** 徽章。
- 与状态区之间：**1px 竖线**（`border-right` + `padding-right`），窄屏改为**横线分隔**（全宽品牌行 + `border-bottom`）。

### 6.2 状态行（`status-item`）

- 字号：`--fs-subhead`，默认色 `--text-3`，`strong` 用 `--text-2`。
- 项间竖分割：浅色 `rgba(0,0,0,0.14)` 细线（深色主题建议后续改为 token 化）。

### 6.3 图标按钮（`icon-btn`）

- 40×40，`--r-sm`，`bg-control` + `border`，悬停 `bg-hover` + `border-hover`。

### 6.4 主/次药丸（`tl-pill` / `tl-pill--primary` / `tl-pill--secondary`）

- 高度约 40px，全圆角，`--fs-subhead`，字重 600；**内联 flex**，`gap: 8px`，便于主按钮左侧放置 **`.tl-pill-ic`**（16×16，`stroke: currentColor`，`fill: none`，`stroke-width` 约 1.85，圆角端点与 `summary-card-head` 的 `.sys-ic` 一致）。
- **主按钮**：填充 `--primary`，字色白，悬停 `--primary-hover`。
- **次按钮**：`bg-control` + `border`，悬停同 icon-btn。

### 6.4.1 Inspector 底部同步提示（`inspector-sync-hint`）

- **位置**：`#inspectorScroll` 与 `.inspector-footer` 之间，`flex-shrink: 0`，随 Inspector 列固定在模块底部区域（不进入滚动区）。
- **结构**：横向 `flex`，左侧 **`inspector-sync-hint__jump`**（约 34×34 图标按钮，`aria-label` 说明「聚焦到同步到键盘」），右侧 **`inspector-sync-hint__text`**（`--fs-caption1`，默认 `--text-3`；**`--ready`** 时提升至 `--text-2`）；`role="note"` + `aria-live="polite"`。
- **语义**：`badge === 已就绪` 时展示「修改配置文件后…同步到键盘…写入硬件」；`待同步` / 其它状态对应较短提示；**已就绪** 时隐藏 `.inspector-footer` 内的 **`meta-badge`**，避免再显示「已就绪」三字。

### 6.5 Meta 徽章（`meta-badge`）

- 用于「当前选中」「Mode 摘要」「同步状态」等**只读标签**。
- `bg-control` + `border`，`--fs-footnote`，`--fw-medium`，文案 `--text-2`。

### 6.6 侧栏导航（`nav-item`）

- 未选中：`--text-3`，`--fs-subhead` + medium。
- 选中：背景为 `color-mix(text-1 6%, transparent)`，文字 `--text-1` + semibold。
- 左侧图标格：`bg-control` + 圆角小方块，与系统侧边栏列表一致。
- **点击反馈**：`:active` 时 **`transform: scale(0.97)`**，与背景/描边色过渡同用约 **160ms**，避免与视图切换动画抢戏。

### 6.7 Inspector 内 Panel（`.inspector .panel`）

- 表面 `bg-control`，圆角 `--r-md`，**不使用**大模糊与重阴影（与顶栏工作台统一）。
- **主配置区**（如「语音方案」）标题：`--fs-subhead` + semibold + `--text-1`；正文 `--fs-footnote` + `--text-2`。
- **备注区**见 **§6.12**，用于绑定摘要、交付说明等辅助信息，层级低于主配置。
- **设备信息 / AI 右栏**：`.device-info-view .panel`、`.ai-engine-view .panel` 与上表**同一套 token**；`panel--remark` 与 Inspector 备注区视觉一致。

### 6.8 选项行（`option-row`）

- 左右结构：左标签右值；`bg-card` 或浅色下白底卡片感，圆角 `--r-md`。

### 6.9 状态/语音药丸（`state-pill` / `voice-pill`）

- 默认：`bg-control` + `border`，`--fs-footnote`。
- 选中：主色低混合背景 + 边框（`color-mix` with `--primary`），文字 `--text-1`。

### 6.10 侧栏试用卡（`sb-trial`）

- Typeless 式：小标题 uppercase、细进度条、**圆角蓝填充按钮「升级」**。
- 与全局 **Pro Trial 徽章**语义一致：侧栏卡负责**转化与说明**，顶栏徽章负责**一眼识别身份**。

### 6.11 画布底部摘要条（`summary-band` / `summary-card`）

- **布局**：四列网格（Mode / OLED / 灯条 / 拨杆），卡片间距与区内边距略收紧（与原型 `gap` / `padding` 一致）。
- **标题行（`summary-card-head`）**：**10px** uppercase、**`--text-3`**，左侧配 **13px 线框图标**（`.sys-ic`，与侧栏/映射条同一套笔画风格）。
- **正文（卡片内 `span`）**：**11px**、`--fw-medium`、`--text-2`，用于一行状态摘要，避免与主画布标题竞争层级。

### 6.12 Inspector 备注区块（`.panel.panel--remark`）

用于 **当前绑定摘要、说明** 等**非主路径配置**（备注 / 辅助阅读），信息层级低于默认 `.inspector .panel`：

- **容器**：半透明 `bg-control` 混合、**虚线边框**、略小的内边距；无额外阴影。
- **区块标题**：`--fs-caption1`、`--fw-medium`、`--text-3`（弱于主 Panel 的 subhead + semibold）。
- **摘要行（`option-row`）**：`--fs-caption1`，标签与值均为 **`--text-3`**，行高与 padding 略收，圆角可用 `--r-sm`。
- **说明段落**：`--fs-caption1` + **`--text-3`**；段内需要强调的词句可用 **`--text-2`** + semibold（仍低于主正文 `--text-2` footnote 档）。

---

## 7. 主题切换约定

- **显式主题**：`document.documentElement.setAttribute('data-theme', 'light' | 'dark')`，并可写入 `localStorage`（原型键名 `ahakey_theme`）。
- **自动**：去掉 `data-theme`，由 `@media (prefers-color-scheme: dark)` 下 `:root:not([data-theme])` 提供深色 token。
- **顶栏主题按钮**：循环 auto → dark → light；图标随状态切换（◐ / ☾ / ☀）。

实现新组件时：**禁止写死 #fff/#000 作为大面积背景**，应使用 `bg-*` / `text-*` / `border`。

### 7.1 SwiftUI 客户端白天 / 黑夜模式

- **入口位置**：主题切换只放在顶栏右侧操作组，使用 SF Symbols 图标按钮；白天为 `sun.max`，黑夜为 `moon.fill`，不使用文字按钮抢主操作层级。
- **状态持久化**：SwiftUI 客户端使用 `AhaKeyAppearanceMode.storageKey` 保存 `light | dark`；主工作台、新手引导、调试预览使用同一个偏好。
- **白天模式**：`windowBackgroundColor / controlBackgroundColor / textBackgroundColor` 为主背景层，卡片边界使用 1px 语义描边，主文本 `primary`，辅助文本 `secondary`。
- **黑夜模式**：保持同一层级关系，不反转信息架构；大面积背景仍用系统语义色，卡片只轻微提亮，边框提升到 `borderStrong` 时仍不使用重阴影。
- **按钮对比**：onboarding 主按钮白天为黑底白字，黑夜为白底黑字；次按钮保持系统浅/深色控制面 + 描边，确保在两个主题下同级按钮高度、字号和按压反馈一致。
- **语音悬浮球**：始终使用黑色胶囊作为录音反馈，位置固定在主窗口底部中央；窗口不可见时贴屏幕底部中央，保证最小化后仍可见。
- **新手引导窗口**：首次打开时必须覆盖整个客户端内容区，不使用居中小弹窗；调试预览最小尺寸与主窗口一致（1280×820），避免布局验收与实际首次打开不一致。

---

## 8. 与代码的对应关系

| 文档概念 | 原型中的主要载体 |
|-----------|------------------|
| Token 定义 | `ahakey-ux-review.html` 内 `<style>` 的 `:root` / `[data-theme="dark"]` |
| 上中下带间距 | `:root { --shell-gap: 5px }` → `.app-shell { gap: var(--shell-gap) }` |
| 顶栏 | `.toolbar.workbench-header` |
| 品牌 | `.toolbar-brand` + `.sb-brand-left` / `.sb-mark` / `.sb-title` / `.sb-badge` |
| 工作台 | `.workspace` + `.main-area`；`.workbench-split` 默认 **`65fr` / `35fr`**；`data-view="device-info" \| "ai"` 时为 **`1fr` / `1fr`** |
| 设备信息 / AI 面板 | `#viewDeviceInfo` / `#viewAi`（与 `#workbenchInspector` 同槽） |
| 配置底栏 | `.inspector-footer` + `.tl-pill`（主按钮可含 `.tl-pill-ic`） |
| Inspector 同步提示 | `#inspectorSyncHint` + `.inspector-sync-hint__jump` / `__text` |
| Inspector 备注 | `.inspector .panel.panel--remark`（如语音键下的绑定摘要、说明） |
| 首次语音 Onboarding | `#onboardingBubble`、`#micSheet`、`#voiceFloat`、`#privacyStrip`、`#globalToast`；脚本内 `OnboardingManager`；`localStorage` 键 `ahakey_onboarding_v2` |
| Typeless 式统一引导 | `#unifiedTypelessTour`（`.ut-tour`）；脚本 `UnifiedTypelessTour`；`localStorage` 键 `ahakey_unified_tour_v1`；右侧氛围图 `assets/typeless-onboarding-ref-bg.png`、`typeless-settings-mockup-bg.png`（设置步）、`typeless-mic-test-bg.png`（麦克风步） |

后续若拆到 SwiftUI，建议建立 **Color assets / Typography styles** 与上表 **同名或一一映射**，便于设计和工程对齐。

---

## 9. 首次体验与 Onboarding（与原型一致）

### 9.1 Typeless 式统一引导（注册 → 设置 → 麦克风 → 体验一下）

全屏层 `#unifiedTypelessTour`：**顶栏四步进度** + **左栏** + **右栏随步骤切换**：注册与「体验一下」步右侧为**隐私说明卡片**；**设置**步为 Typeless 式**权限卡片**（标题 + 说明 +「允许」药丸按钮）+ 右侧**系统引导氛围图**；**麦克风**步对标 Typeless「口述测试」：左侧文案与「是的，继续 / 不，换个麦克风」，右侧**蓝色电平条动画**（`prefers-reduced-motion` 下为静态条）。  
完成后派发 `ahakey-unified-tour-done`；**已走完统一引导的用户**在首次语音成功后**不再展示**底部 `#privacyStrip`。老用户（`ahakey_onboarding_v2.firstVoiceComplete`）不再弹全屏 Tour。`?fresh=1` 同时清空两枚 `localStorage` 键。

### 9.2 首次语音（工作台内）

目标：**第一次通过键盘完成语音输入（First Voice Input）**。统一引导结束后出现**单句气泡**；麦克风若未在引导中勾选，仍可在**首次按语音键**时用 `#micSheet` 触发授权（与真机系统弹窗对应）。

| 模块（原型） | 职责 |
|--------------|------|
| `OnboardingManager` | `localStorage`（`ahakey_onboarding_v2`）、气泡位置、首轮语音拦截、toast；`#privacyStrip` 仅当未完成统一引导链时作为兜底 |
| 麦克风 Sheet | 首次按语音键且未授权时展示；文案「需要麦克风权限才能听到你说话」+ 允许 / 稍后 |
| 语音悬浮 `#voiceFloat` | 状态：`ready`（呼吸约 0.8s ease-in-out，阶段约 420ms）→ `listening` → `processing` → `output` / `error`；错误副本：没听到声音 / 说得有点短 / 环境有点吵 |
| 隐私条 `#privacyStrip` | 兜底：未完成 Typeless 统一引导时，在**首次成功转写之后**展示；标题「你的语音，只属于你」；继续 **3s** 后可用 |
| 底栏 | **重置首次体验**（两枚 `localStorage` 均清空并重新打开统一引导）；旧版 Tour / 权限向导收入 **`<details>`** |

演示：`?onbErr=silent|short|noise` 模拟一次错误识别。

---

## 10. 修订记录

| 日期 | 说明 |
|------|------|
| 2026-05-02 | 初版及迭代：`ahakey-ux-review.html` 对齐；画布 **摘要卡** 小字号 + 图标；Inspector **`panel--remark`**（「当前绑定摘要」「说明」）备注降层级。 |
| 2026-05-02 | 三带间距 **`--shell-gap` 改为 5px**；Inspector **底部同步提示条**（替代「已就绪」徽章冗长说明）+ **「同步到键盘」主按钮线框图标**；规范 §4 / §5 / §6.4 / §6.4.1 / §8 同步。 |
| 2026-05-02 | **设备信息 / AI** 与画布 **1:1 分栏**、视图切换 **左滑缩小 / 左滑扩大**（`workbench-split--to-device` / `--to-workbench`）；设备信息 **Panel / remark / meta-badge** 与 Inspector 对齐；§5.2 / §6.6 / §6.7 / §8 / §9 同步。 |
| 2026-05-02 | 视图切换动效：**去掉 scale 与过冲贝塞尔**，改为 **translate + opacity**、**`--ease-product-decel`**、**`--view-switch-ms` 320ms**；**`prefers-reduced-motion`** 关闭动画；§5.2 与 §9 同步。 |
| 2026-05-02 | 二度优化：**键盘不做位移动画**；右栏 **soft-in**；**`--view-switch-ms` 400ms**；孪生 **键帽/标签/OLED** 与 **容器查询映射行** 同比例缩小；§5.2 / §9 同步。 |
| 2026-05-02 | **§9 首次语音 Onboarding**：状态驱动首访、触发式麦克风 Sheet、语音悬浮状态机、隐私条与 toast；底栏重置 + 旧版引导折叠；原 §9 修订表顺延为 **§10**。 |
| 2026-05-02 | **§9.1 Typeless 统一引导**：全屏 `#unifiedTypelessTour` + `UnifiedTypelessTour` + 资源 `assets/typeless-onboarding-ref-bg.png`；与首次语音衔接；§8 / §9 / §10 同步。 |
| 2026-05-02 | **统一引导 UI**：四步（含麦克风试音）；设置页 Typeless 式权限卡片 + 右侧 `typeless-settings-mockup-bg`；麦克风页电平条动画 + `typeless-mic-test-bg`；体验一下右侧摘要卡；§8 / §9 同步。 |

如需把本规范同步到 Figma Variables 或 SwiftUI `Theme`，可在同一目录追加子文档说明映射表。
 
