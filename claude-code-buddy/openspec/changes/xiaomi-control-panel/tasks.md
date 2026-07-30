# Tasks: xiaomi-control-panel

## 1. 设置层扩展（先把可调项备齐）

- [ ] 1.1 `settings.{h,cpp}` 新增三项：音色 `vvoice`（字符串）、人设 `vpersona`（字符串）、
      说话跳舞 `vdance`（bool）；编译期常量降级为出厂默认，NVS 有值则覆盖
- [ ] 1.2 `realtime_ws.cpp` 的 `session.update` 改从 settings 取音色与人设（不再用常量）
- [ ] 1.3 语音固件接入"说话时跳舞"：SPEAKING 状态按开关选择活泼摆动 / 轻摆 / 不动
- [ ] 1.4 双 env 编译绿；真机验证改设置后行为符合（含出厂默认路径）

## 2. 状态快照与暂存写入（跨任务安全的地基）

- [ ] 2.1 定义状态快照结构（FSM 状态/连接/轮次/上轮延迟与 token/underrun/电量/字幕），
      主循环单向发布，只读给外部
- [ ] 2.2 定义 pending 设置结构：HTTP 任务只写、主循环取走并应用+持久化（不在 HTTP
      任务里碰外设或 NVS）
- [ ] 2.3 单元测试（native env）：范围夹取逻辑、pending 应用一次性语义

## 3. 设备端 HTTP 服务

- [ ] 3.1 `web_panel.{h,cpp}`：独立 FreeRTOS 任务（core 0，优先级低于播放任务）起
      `WebServer`；首次连上 WiFi 后启动，WiFi 断开时停止
- [ ] 3.2 `GET /api/state` — 返回快照
- [ ] 3.3 `GET /api/settings` / `POST /api/settings` — 读、部分更新、范围夹取、
      返回应用后实际值 + 标注延迟生效项
- [ ] 3.4 `POST /api/action` — `dance` / `disconnect`，动作关闭时 dance 返回说明
- [ ] 3.5 首次连上 WiFi 后在字幕带显示面板地址

## 4. 面板网页落地

- [ ] 4.1 把原型 HTML（artifact 879f3954）改造成真页面：轮询 `/api/state`、
      表单提交走 `/api/settings`、动作按钮走 `/api/action`
- [ ] 4.2 延迟生效项（音色/人设）在 UI 上明确标注"下次唤醒生效"
- [ ] 4.3 对话记录按 design 开放问题的结论实现（默认只显示最近一轮）
- [ ] 4.4 `tools/gen_panel_html.py`：构建期把 HTML gzip 成 `panel_html.h`；
      接进 platformio extra_scripts，改 HTML 后重新构建即更新

## 5. 真机验收

- [ ] 5.1 冒烟：唤醒 → 浏览器打开面板 → 看到实时状态随对话变化
- [ ] 5.2 设置验收：即时项立刻生效、延迟项下次会话生效、非法值被夹、重启后保留
- [ ] 5.3 动作验收：试跳生效；总开关关闭时不动
- [ ] 5.4 **不干扰验收**（关键）：边对话边轮询面板，本轮 `underruns=0 dry=0ms`，
      松手检测正常
- [ ] 5.5 `make test` 全绿；README 补面板小节（地址怎么找、能调什么、安全边界）
- [ ] 5.6 用户 review + commit + push
