## 1. 素材转换

- [x] 1.1 从 upstream `rullerzhou-afk/clawd-on-desk` 的 `assets/gif/clawd-idle-reading.gif`
      （302×300，60帧，透明混合背景）转换到 `data/characters/clawd/clawd-idle-reading.gif`
      （120×115，60帧）。**实测发现跟设计假设不同**：没有照搬 `claude-code-buddy/tools/
      clawd-gif/recrop2.sh` 的参数（那套是给 Tab5 220 方形框、主体只占 ~45%、大量留白
      给 props），而是量出本项目已有 `idle.gif` 的真实约定——主体几乎填满画布（116×68
      / 120×80 画布,四周仅留 2-6px）——按同一约定裁剪：PIL 逐帧取非透明像素 bbox 并集
      定位内容框、加 ~6px padding、`magick -coalesce -background black -alpha remove
      -alpha off` 摊平透明为纯黑完整帧、裁剪、`-resize 120x` 等比缩放、`+remap` 强制
      单一全局调色板。方法已同步记入 design.md Decisions。
- [x] 1.2 已核对：单一全局调色板（`identify -format "%[gif:local-colormap]"` 为空，
      非逐帧局部调色板）；主体大小与 `idle.gif` 紧裁剪风格一致；60 帧、100KB 文件大小
      在现有素材区间内（对比 busy_0.gif 130KB / idle.gif 200KB）。真机渲染效果留给
      任务 3.1 验证（调色板/帧编码正确性只能在本地静态核对，解码器实际表现需真机）。

## 2. 固件接线

- [x] 2.1 `clawd_player.cpp` 新增 `idleVariant_`（0/1）+ `idleNextMs_` 计时状态
      （紧邻现有 `busyVariant_`/`busyNextMs_`），`fileForState()` 的 `Idle` 分支按
      `idleVariant_` 返回 `idle.gif` 或 `clawd-idle-reading.gif`。额外补了一处边界
      处理：`setState()` 从非 Idle 重新进入 Idle 时把 `idleVariant_` 归零，避免沿用
      上次离开 Idle 时可能停留的 reading 变体。
- [x] 2.2 `tick()` 新增判定：仅当 `baseState_==Idle && !sleeping_ && reactionMs_<=0`
      时，到 `idleNextMs_` 才判定一次切换——variant 0 时 ~20% 概率切到 1（停留 5s），
      variant 1 时到点必定切回 0（切回后 10s 后再判下一次）；用 Arduino 标准
      `random(100)`（项目里首次引入随机数，ESP32 Arduino core 底层走硬件 RNG，
      不需要手动 seed）；两种情况都 `applyTarget()` 重开文件。
- [x] 2.3 `pio run -e cardputer-adv` 编译通过（RAM 25.3%, Flash 43.9%，无警告）；
      `pio run -e cardputer-adv -t buildfs` 确认 `clawd-idle-reading.gif` 已打进
      littlefs 镜像。

## 3. 真机验证

- [ ] 3.1 flash app + littlefs 到真机，静置在 Idle 态观察一段时间，确认能看到
      `clawd-idle-reading.gif` 偶尔出现、停留几秒后自动切回 `idle.gif`，且切换频率
      不过于频繁（不像"抽搐"）也不会长时间完全不出现。
- [ ] 3.2 验证真实状态变化（如收到一个新 Claude 会话进入 Thinking）能立即打断当前
      idle 变体播放，不被计时器延迟。
- [ ] 3.3 验证 sleeping（久不活动进入 sleep.gif）与临时 reaction 动画（heart/dizzy/error）
      期间不会误触发 idle-reading 切换。
