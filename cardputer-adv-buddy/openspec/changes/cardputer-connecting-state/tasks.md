## 1. 素材转换

- [x] 1.1 从 upstream `rullerzhou-afk/clawd-on-desk` 的 `assets/gif/clawd-carrying.gif`
      （302×300，45帧，透明混合背景）转换到 `data/characters/clawd/clawd-carrying.gif`
      （120×96，45帧）。方法同 `cardputer-idle-variety` change 里实测修正过的流程
      （不是 `claude-code-buddy/tools/clawd-gif/recrop2.sh` 的 Tab5 220 方形参数）：
      PIL 逐帧取非透明像素 bbox 并集定位内容框、加 padding、`magick -coalesce
      -background black -alpha remove -alpha off` 摊平透明为纯黑完整帧、裁剪、
      `-resize 120x`、`+remap` 强制单一全局调色板。已同步记入 design.md Decisions。
- [x] 1.2 已核对：单一全局调色板；主体大小与现有素材紧裁剪风格一致；45 帧、100KB
      文件大小在现有素材区间内。真机渲染效果留给任务 3.1 验证。

## 2. 固件接线

- [x] 2.1 `agent_state.h` 新增 `AgentState::Connecting` 枚举值（`Count` 前）。
- [x] 2.2 `clawd_player.cpp` 的 `fileForState()` 新增 `Connecting → "clawd-carrying.gif"`
      分支。
- [x] 2.3 `clawd_player.cpp` 的 `drawSessionTag()` 入口新增 `!online_` 早分支（清同一块
      矩形区、画常驻 `Connecting...` 后 return，不动 efontCN 字体切换）；online 状态
      经新增 `clawd::setOnline(bool)` setter 传入（贴合 `setSleeping` 风格）。
- [x] 2.4 `main.cpp` 轮播块顶部 `clawd::setOnline(online)` + `!online` 分支置
      `target = Connecting`（排在既有 `n == 0` 之前，在线路径逐字不变）；沿用
      `(int)target != lastAg` 边沿捕捉。**关键伴随修正**：`setSleeping(!online || …)`
      改为 `setSleeping(online && idle && still>30s)`——原 `!online` 强制 sleep 且
      `targetFile()` 里 sleep 优先级高于状态 GIF，不摘掉的话 carrying 永远被 sleep.gif
      盖住（spec 阶段发现并锁定的设计点）。README「离线→sleep」描述同步更新。
- [x] 2.5 `pio run -e cardputer-adv` 编译通过（RAM 25.3%, Flash 43.9%，reviewer 亲自
      复跑确认）。littlefs 无需重打：`clawd-carrying.gif` 已随 idle-variety 那次
      uploadfs 烧入真机。
      （本节实现由 cursor-agent 按 handoff spec `001-cardputer-connecting-state.md`
      执行，Fable review 通过。）

## 3. 真机验证

- [x] 3.1 真机验证 ✓：开机 cc-bridge 未连时显示 `clawd-carrying.gif` + 顶栏左常驻
      `Connecting...`，用户确认「完全符合预期」。
- [x] 3.2 真机验证 ✓：连上 cc-bridge 后 Connecting 视觉立即消失、恢复正常渲染，无残留。
- [ ] 3.3 （未专门验证）「已连接但零会话数据」应回退真实 Idle 而非 Connecting——代码路径
      上 `online==true` 时不会进 Connecting 分支（只在 `!online` 进），逻辑正确；真机上
      每次连上都伴随会话数据，未单独构造零会话场景验证。
- [ ] 3.4 （未专门验证）BLE 中途断开重连——本次真机是开机首连场景；重连进出 Connecting
      的边沿依赖固件既有的断连重广播路径，未单独制造中途断连验证。
