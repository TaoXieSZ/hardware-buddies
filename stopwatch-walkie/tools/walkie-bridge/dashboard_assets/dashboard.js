"use strict";

if (location.protocol === "file:") {
  location.replace("http://127.0.0.1:8766/");
}

const $ = (id) => document.getElementById(id);
const timeline = [];
let cursor = 0;
let statusTimer;
let eventTimer;

const stageOrder = ["recording", "transcribing", "routing", "proposal", "running"];
const stageLabels = {
  disconnected: "DISCONNECTED", connected: "CONNECTED", authenticating: "AUTH",
  ready: "READY", recording: "RECORDING", transcribing: "TRANSCRIBING",
  routing: "ROUTING", route_error: "ROUTE ERROR", proposal: "CONFIRM",
  dispatching: "DISPATCH", accepted: "ACCEPTED", running: "RUNNING",
  permission: "PERMISSION", completed: "COMPLETED", failed: "FAILED", cancelled: "CANCELLED"
};

function fixture(stage) {
  const base = {
    bridge: {status: "listening", dashboard: "listening", port: 8765, dashboard_port: 8766},
    watch: {connected: true, authenticated: true, protocol: 2, device_id: "stopwatch-c3"},
    pipeline: {stage: stage, utterance_id: "utt-demo", bytes: 154240, latency_ms: 1138,
      transcript: "Codex 检查一下当前项目的测试", error_code: "", hint: "命令链路正常。", candidates: []},
    control_plane: {healthy: true, revision: 42, sessions: [
      {agent: "codex", label: "hardware-buddies", project_label: "stopwatch", state: "idle", capabilities: {steer: true, permission_reply: true}},
      {agent: "claude", label: "firmware", project_label: "stopwatch", state: "running", capabilities: {steer: true, permission_reply: false}},
      {agent: "opencode", label: "bridge-ui", project_label: "walkie", state: "idle", capabilities: {steer: true, permission_reply: false}}
    ]},
    proposal: null, task: null, permission: null, tts: {state: "idle", bytes: 0},
    updated_at_ms: Date.now(), latest_sequence: 8
  };
  if (stage === "disconnected") {
    base.watch = {connected: false, authenticated: false, protocol: 0, device_id: ""};
    base.pipeline.hint = "StopWatch 已断开，等待自动重连。";
  } else if (stage === "ready") {
    base.pipeline.transcript = ""; base.pipeline.bytes = 0; base.pipeline.latency_ms = 0;
    base.pipeline.hint = "按住 A 说话；命令需以 Agent 或别名开头。";
  } else if (stage === "recording") {
    base.pipeline.transcript = ""; base.pipeline.bytes = 32768; base.pipeline.hint = "正在录音…";
  } else if (stage === "transcribing") {
    base.pipeline.transcript = ""; base.pipeline.hint = "百炼 ASR 正在识别。";
  } else if (stage === "target-required") {
    base.pipeline.stage = "route_error"; base.pipeline.error_code = "target_required";
    base.pipeline.transcript = "检查一下当前项目的测试";
    base.pipeline.hint = "请以 Claude、Codex、OpenCode、Kimi 或已配置别名开头。";
    base.pipeline.candidates = ["hardware-buddies", "firmware", "bridge-ui"];
  } else if (stage === "proposal") {
    base.proposal = {command_id: "cmd-demo", agent: "codex", label: "hardware-buddies", project_label: "stopwatch", text: "检查一下当前项目的测试"};
    base.pipeline.hint = "请在手表上按 A 批准或按 B 拒绝。";
  } else if (stage === "running") {
    base.task = {task_id: "task-demo", command_id: "cmd-demo", state: "running", summary: "", code: ""};
    base.pipeline.hint = "Agent 正在执行。";
  } else if (stage === "waiting-permission") {
    base.pipeline.stage = "permission"; base.task = {task_id: "task-demo", state: "running", summary: "", code: ""};
    base.permission = {request_id: "req-demo", agent: "codex", hint: "允许执行测试命令", actionable: true};
    base.pipeline.hint = "等待手表上的权限确认。";
  } else if (stage === "completed") {
    base.task = {task_id: "task-demo", state: "completed", summary: "测试通过：42 passed", code: ""};
    base.pipeline.hint = "测试通过：42 passed";
  } else if (stage === "failed") {
    base.task = {task_id: "task-demo", state: "failed", summary: "", code: "control_plane_unavailable"};
    base.pipeline.error_code = "control_plane_unavailable"; base.pipeline.hint = "cc-bridge 控制面不可用。";
  }
  return base;
}

function setHealth(id, ok, warning = false) {
  $(id).className = `signal ${ok ? "ok" : warning ? "warn" : ""}`;
}

function formatBytes(value) {
  if (!value) return "0 B";
  if (value < 1024) return `${value} B`;
  return `${(value / 1024).toFixed(1)} KB`;
}

function renderStatus(data) {
  const watch = data.watch || {};
  const bridge = data.bridge || {};
  const control = data.control_plane || {};
  const pipe = data.pipeline || {};
  const stage = pipe.stage || "disconnected";

  $("bridge-status").textContent = bridge.status === "listening" ? `监听 :${bridge.port || 8765}` : "启动中";
  $("watch-status").textContent = watch.connected ? (watch.device_id || "已连接") : "未连接";
  $("auth-status").textContent = watch.authenticated ? `V${watch.protocol} 已认证` : "等待";
  $("control-status").textContent = control.healthy ? `在线 · R${control.revision || 0}` : "离线";
  setHealth("bridge-dot", bridge.status === "listening");
  setHealth("watch-dot", Boolean(watch.connected));
  setHealth("auth-dot", Boolean(watch.authenticated), Boolean(watch.connected));
  setHealth("control-dot", Boolean(control.healthy));
  $("updated-at").textContent = data.updated_at_ms ? new Date(data.updated_at_ms).toLocaleTimeString("zh-CN", {hour12: false}) : "--";

  const stageBadge = $("stage-badge");
  stageBadge.textContent = stageLabels[stage] || stage.toUpperCase();
  stageBadge.className = `stage-badge ${["failed", "route_error"].includes(stage) ? "error" : ["permission", "proposal"].includes(stage) ? "warn" : ""}`;
  const activeIndex = stage === "accepted" || stage === "dispatching" ? 4 : stage === "permission" || stage === "completed" || stage === "failed" ? 4 : stageOrder.indexOf(stage);
  document.querySelectorAll(".pipeline li").forEach((item, index) => {
    item.classList.toggle("active", index === activeIndex);
    item.classList.toggle("done", activeIndex > index || stage === "completed");
  });
  $("audio-bytes").textContent = formatBytes(pipe.bytes);
  $("asr-latency").textContent = pipe.latency_ms ? `${pipe.latency_ms} ms` : "—";
  $("tts-state").textContent = String((data.tts || {}).state || "idle").toUpperCase();
  $("transcript").textContent = pipe.transcript || "等待从 StopWatch 收到语音。";
  $("routing-hint").textContent = pipe.hint || "等待新事件。";
  $("routing-hint").classList.toggle("error", Boolean(pipe.error_code));
  $("error-code").textContent = pipe.error_code || "NO ERROR";
  $("error-code").className = `code-tag ${pipe.error_code ? "error" : ""}`;
  $("candidates").replaceChildren(...(pipe.candidates || []).map(value => {
    const node = document.createElement("span"); node.textContent = value; return node;
  }));

  const sessions = control.sessions || [];
  $("session-count").textContent = String(sessions.length).padStart(2, "0");
  if (!sessions.length) {
    $("session-list").innerHTML = '<p class="empty">控制面暂未返回会话。</p>';
  } else {
    $("session-list").replaceChildren(...sessions.map(session => {
      const card = document.createElement("div"); card.className = "session-card";
      const bar = document.createElement("span"); bar.className = "bar";
      const body = document.createElement("div");
      const title = document.createElement("strong"); title.textContent = session.label || "未命名会话";
      const project = document.createElement("span"); project.textContent = `${session.project_label || "—"} · ${session.state || "unknown"}`;
      body.append(title, project);
      const agent = document.createElement("span"); agent.className = "agent"; agent.textContent = session.agent || "agent";
      card.append(bar, body, agent); return card;
    }));
  }

  const proposal = data.proposal;
  const task = data.task;
  $("proposal-target").textContent = proposal ? `${proposal.agent} / ${proposal.label || proposal.project_label}` : "—";
  $("proposal-text").textContent = proposal ? proposal.text : "尚无待确认命令";
  $("task-summary").textContent = task ? (task.summary || task.code || task.state) : "—";
  $("task-state").textContent = task ? String(task.state).toUpperCase() : "IDLE";
  $("task-state").className = `stage-badge ${task && task.state === "failed" ? "error" : task && task.state === "running" ? "" : "muted"}`;
  const permission = data.permission;
  $("permission-box").hidden = !permission;
  $("permission-text").textContent = permission ? `${permission.agent}: ${permission.hint}` : "";
}

function renderTimeline() {
  if (!timeline.length) {
    const empty = document.createElement("li"); empty.className = "empty"; empty.textContent = "等待第一个事件。";
    $("timeline").replaceChildren(empty); return;
  }
  $("timeline").replaceChildren(...timeline.slice(0, 30).map(event => {
    const row = document.createElement("li");
    const time = document.createElement("time"); time.textContent = new Date(event.timestamp_ms).toLocaleTimeString("zh-CN", {hour12: false});
    const kind = document.createElement("code"); kind.textContent = event.type;
    const detail = document.createElement("span");
    detail.textContent = event.code || event.summary || event.text || event.label || event.device_id || `#${event.sequence}`;
    row.append(time, kind, detail); return row;
  }));
}

async function fetchJson(url) {
  const response = await fetch(url, {cache: "no-store"});
  if (!response.ok) throw new Error(`HTTP ${response.status}`);
  return response.json();
}

async function refreshStatus() {
  try { renderStatus(await fetchJson("/api/status")); }
  catch (_error) {
    $("bridge-status").textContent = "看板离线"; setHealth("bridge-dot", false);
    $("routing-hint").textContent = "无法连接本地看板 API；WebSocket bridge 可能仍在运行。";
    $("routing-hint").classList.add("error");
  }
}

async function refreshEvents() {
  try {
    const requestedCursor = cursor;
    const page = await fetchJson(`/api/events?after=${requestedCursor}`);
    if (page.gap || page.next_sequence < requestedCursor) {
      cursor = 0; timeline.length = 0; renderTimeline(); await refreshStatus(); return;
    }
    cursor = page.next_sequence;
    timeline.unshift(...page.events.reverse());
    if (timeline.length > 30) timeline.length = 30;
    renderTimeline();
  } catch (_error) { /* status poll owns the visible offline state */ }
}

function schedule() {
  clearInterval(statusTimer); clearInterval(eventTimer);
  const hidden = document.hidden;
  statusTimer = setInterval(refreshStatus, hidden ? 4000 : 1000);
  eventTimer = setInterval(refreshEvents, hidden ? 4000 : 750);
}

const fixtureName = new URLSearchParams(location.search).get("fixture");
if (fixtureName) {
  renderStatus(fixture(fixtureName));
  timeline.push({sequence: 8, timestamp_ms: Date.now(), type: fixtureName === "target-required" ? "route.failed" : `fixture.${fixtureName}`, code: fixtureName === "target-required" ? "target_required" : ""});
  renderTimeline();
} else {
  refreshStatus(); refreshEvents(); schedule();
  document.addEventListener("visibilitychange", () => { refreshStatus(); refreshEvents(); schedule(); });
}

setInterval(() => { $("local-time").textContent = new Date().toLocaleTimeString("zh-CN", {hour12: false}); }, 1000);
$("local-time").textContent = new Date().toLocaleTimeString("zh-CN", {hour12: false});
