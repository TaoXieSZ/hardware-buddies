// dsh-walkie — StopWatch walkie-bridge 的 DSH 编排层插件。
// 操作面:walkie_status / walkie_events / walkie_propose / walkie_resolve /
//         walkie_say / walkie_wait(任意 DSH 会话可用)。
// 语音路由脑:专职 headless 值班会话,长轮询 bridge 转写队列,硬化 prompt →
// 结构化 JSON → POST decision。手表圆屏确认与 bridge 侧白名单是安全闸,
// 本插件只是大脑。
//
// 只依赖 node 内建 + 相对 ./lib.js(本地文件插件不可依赖裸包名解析);
// ctx.tools / ctx.apiProxy 由 DSH host 直接提供。

import { randomUUID } from 'node:crypto'
import { buildRoutingPrompt, loadConfig, parseDecision, validateDecision } from './lib.js'

export const name = 'dsh-walkie'
export const inject = ['tools', 'apiProxy']

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms))

// ── HTTP client ──────────────────────────────────────────────────────────────

function makeClient(cfg) {
  const headers = { 'Content-Type': 'application/json' }
  if (cfg.token) headers.Authorization = `Bearer ${cfg.token}`
  async function call(path, { method = 'GET', body, signal } = {}) {
    let response
    try {
      response = await fetch(cfg.base_url + path, {
        method,
        headers,
        signal,
        body: body === undefined ? undefined : JSON.stringify(body),
      })
    } catch {
      return { ok: false, error: 'bridge_unreachable' }
    }
    if (!response.ok) {
      let error = `http_${response.status}`
      try {
        const parsed = await response.json()
        if (parsed?.error) error = parsed.error
      } catch {}
      return { ok: false, error }
    }
    try {
      return await response.json()
    } catch {
      return { ok: false, error: 'invalid_json' }
    }
  }
  return {
    status: (signal) => call('/api/v1/status', { signal }),
    events: (after, limit, signal) => call(`/api/v1/events?after=${after}&limit=${limit}`, { signal }),
    popQueue: (waitMs, signal) => call(`/api/v1/brain/queue?wait_ms=${waitMs}`, { signal }),
    decide: (itemId, decision, signal) => call('/api/v1/brain/decision', {
      method: 'POST', body: { item_id: itemId, decision }, signal,
    }),
    propose: (text, selector, signal) => call('/api/v1/proposals', {
      method: 'POST', body: { text, selector }, signal,
    }),
    speak: (text, signal) => call('/api/v1/tts', { method: 'POST', body: { text }, signal }),
  }
}

function withTimeout(signal, ms) {
  if (signal == null) return AbortSignal.timeout(ms)
  if (typeof AbortSignal.any === 'function') return AbortSignal.any([signal, AbortSignal.timeout(ms)])
  const controller = new AbortController()
  const abort = () => controller.abort()
  signal.addEventListener('abort', abort, { once: true })
  const timer = setTimeout(() => controller.abort(), ms)
  controller.signal.addEventListener('abort', () => clearTimeout(timer), { once: true })
  return controller.signal
}

const jsonOutput = {
  schema: { type: 'object', additionalProperties: true },
  render: (_args, value) => [{ type: 'text', text: JSON.stringify(value, null, 2) }],
}

// ── 工具注册 ────────────────────────────────────────────────────────────────

function registerWalkieTools(ctx, client) {
  const registry = ctx.tools

  registry.register({
    name: 'walkie_status',
    description: '读取 StopWatch walkie-bridge 的运行时快照(watch 连接、流水线阶段、控制面会话、最近任务/权限)。',
    parameters: { type: 'object', properties: {}, additionalProperties: false },
    output: jsonOutput,
    async execute(_args, exec) {
      return client.status(withTimeout(exec.signal, 8000))
    },
  })

  registry.register({
    name: 'walkie_events',
    description: '读取 walkie-bridge 的运行时事件流(after=游标,limit=条数),用于追踪 brain 路由、提案、任务终态。',
    parameters: {
      type: 'object',
      properties: {
        after: { type: 'integer', description: '事件游标,只取序号大于它的' },
        limit: { type: 'integer', description: '最多返回条数(1-100,默认 50)' },
      },
      additionalProperties: false,
    },
    output: jsonOutput,
    async execute(args, exec) {
      return client.events(args.after ?? 0, args.limit ?? 50, withTimeout(exec.signal, 8000))
    },
  })

  registry.register({
    name: 'walkie_propose',
    description: '向 StopWatch 手表推一条 steer 提案:命中 bridge 白名单正则会直接注入目标会话(gated=false),否则在圆屏等待 KEYA 批准/KEYB 拒绝(gated=true)。agent/label/project_label 是目标选择器,必须与快照里的会话一致。',
    parameters: {
      type: 'object',
      properties: {
        text: { type: 'string', description: '发给目标 agent 的指令文本' },
        agent: { type: 'string', description: '目标 agent:claude/codex/opencode/kimi(可选)' },
        label: { type: 'string', description: '目标会话 label(可选)' },
        project_label: { type: 'string', description: '目标项目标签(可选)' },
      },
      required: ['text'],
      additionalProperties: false,
    },
    output: jsonOutput,
    async execute(args, exec) {
      const selector = {}
      for (const key of ['agent', 'label', 'project_label']) {
        if (typeof args[key] === 'string' && args[key].trim()) selector[key] = args[key].trim()
      }
      const result = await client.propose(args.text, selector, withTimeout(exec.signal, 8000))
      if (result?.gated === true) {
        result.hint = '提案已在手表圆屏上,请用户按 KEYA 批准或 KEYB 拒绝;用 walkie_events 追踪 task 终态。'
      }
      return result
    },
  })

  registry.register({
    name: 'walkie_resolve',
    description: '对 walkie-bridge 队列里的权限请求做仲裁:approve 或 deny。与手表按键 first-response-wins,迟到的应答会被 cc-bridge 拒绝。',
    parameters: {
      type: 'object',
      properties: {
        approval_id: { type: 'string', description: '队列里 permission 工作项的 item_id 或 request_id' },
        decision: { type: 'string', description: 'approve 或 deny' },
      },
      required: ['approval_id', 'decision'],
      additionalProperties: false,
    },
    output: jsonOutput,
    async execute(args, exec) {
      if (args.decision !== 'approve' && args.decision !== 'deny') {
        return { ok: false, error: 'invalid_decision', hint: 'decision 必须是 approve 或 deny' }
      }
      return client.decide(args.approval_id, { kind: 'permission', decision: args.decision },
        withTimeout(exec.signal, 8000))
    },
  })

  registry.register({
    name: 'walkie_say',
    description: '让 StopWatch 手表用 TTS 播报一段文字(最长 400 字符)。手表不在线返回 watch_offline。',
    parameters: {
      type: 'object',
      properties: { text: { type: 'string', description: '要播报的文字' } },
      required: ['text'],
      additionalProperties: false,
    },
    output: jsonOutput,
    async execute(args, exec) {
      return client.speak(args.text, withTimeout(exec.signal, 8000))
    },
  })

  registry.register({
    name: 'walkie_wait',
    description: '长轮询 walkie-bridge 的 brain 队列,取一条待处理工作项(transcript 转写待路由 / permission 权限待仲裁),无则等待至超时。主要用于值班/观察场景。',
    parameters: {
      type: 'object',
      properties: { wait_ms: { type: 'integer', description: '最长等待毫秒(默认 25000,上限 60000)' } },
      additionalProperties: false,
    },
    output: jsonOutput,
    async execute(args, exec) {
      return client.popQueue(args.wait_ms ?? 25000, withTimeout(exec.signal, 65000))
    },
  })
}

// ── 值班 headless 会话(语音路由脑) ──────────────────────────────────────────

function request(payload) {
  return { rpcId: `dsh-walkie-${randomUUID()}`, payload }
}

function extractSessionId(session) {
  const header = session?.header ?? session?.meta ?? {}
  return String(header.id ?? session?.id ?? '')
}

function extractMessageText(event) {
  const message = event?.data?.message
  if (!message) return ''
  const content = message.content
  if (typeof content === 'string') return content
  if (Array.isArray(content)) {
    return content
      .filter((block) => block && block.type === 'text' && typeof block.text === 'string')
      .map((block) => block.text)
      .join('\n')
  }
  return ''
}

async function resolveWorkspaceId(api, preferred) {
  try {
    const listed = await api.workspace.list(request({}))
    if (!listed.result?.ok) return null
    const items = Array.isArray(listed.result.value?.items) ? listed.result.value.items : []
    if (!items.length) return null
    return items.find((item) => item.workspaceId === preferred)?.workspaceId
      ?? items[0]?.workspaceId ?? null
  } catch {
    return null
  }
}

function startDutyLoop(ctx, cfg, client) {
  const api = ctx.apiProxy
  if (!api) {
    console.warn('[dsh-walkie] duty disabled: no ctx.apiProxy in this profile')
    return
  }
  let dutySessionId = null
  let turnWaiter = null // { sessionId, resolve, timer }
  let inFlight = false

  ctx.on('session/event', (session, event) => {
    if (event?.type !== 'assistant/message') return
    const sid = extractSessionId(session)
    if (!turnWaiter || turnWaiter.sessionId !== sid) return
    const waiter = turnWaiter
    turnWaiter = null
    clearTimeout(waiter.timer)
    waiter.resolve(extractMessageText(event))
  })

  async function ensureDutySession() {
    if (dutySessionId) return dutySessionId
    const workspaceId = await resolveWorkspaceId(api, cfg.duty.workspaceId)
    const created = await api.sessions.create(request({
      ...(workspaceId ? { workspaceId } : {}),
    }))
    if (!created.result?.ok) throw new Error(`duty session create failed: ${created.result?.error?.code ?? 'unknown'}`)
    const sessionId = created.result.value.sessionId
    try {
      await api.sessions.rename(request({ sessionId, title: 'walkie-duty' }))
    } catch {}
    dutySessionId = sessionId
    console.warn(`[dsh-walkie] duty session ready: ${sessionId}`)
    return sessionId
  }

  function waitForTurn(sessionId, timeoutMs) {
    return new Promise((resolve) => {
      const timer = setTimeout(() => {
        if (turnWaiter?.sessionId === sessionId) turnWaiter = null
        resolve(null)
      }, timeoutMs)
      turnWaiter = { sessionId, resolve, timer }
    })
  }

  async function handleTranscript(item) {
    const sessionId = await ensureDutySession()
    const prompt = buildRoutingPrompt(item.text, item.sessions)
    const sent = await api.sessions.prompt(request({
      sessionId,
      mode: 'queue',
      content: [{ type: 'text', text: prompt }],
    }))
    if (!sent.result?.ok) throw new Error(`duty prompt failed: ${sent.result?.error?.code ?? 'unknown'}`)
    const answer = await waitForTurn(sessionId, 60000)
    const decision = answer == null ? null : parseDecision(answer)
    const validated = decision == null ? { ok: false, reason: 'no json in duty reply' } : validateDecision(decision)
    if (!validated.ok) {
      console.warn(`[dsh-walkie] duty reply invalid for ${item.item_id}: ${validated.reason}`)
      await client.decide(item.item_id, { kind: 'reject' })
      return
    }
    const posted = await client.decide(item.item_id, validated.decision)
    console.warn(`[dsh-walkie] routed ${item.item_id}: kind=${validated.decision.kind} posted=${posted?.ok}`)
  }

  async function loop() {
    for (;;) {
      try {
        if (inFlight) {
          await sleep(1000)
          continue
        }
        const response = await client.popQueue(25000)
        if (!response || response.ok === false) {
          // bridge 不可达时 client 返回 {ok:false} 而不是抛异常;退避后再试,
          // 避免紧循环打爆本机端口。
          await sleep(2000)
          continue
        }
        const item = response?.item
        if (!item || item.kind !== 'transcript') continue
        inFlight = true
        try {
          await handleTranscript(item)
        } finally {
          inFlight = false
        }
      } catch (error) {
        console.warn('[dsh-walkie] duty loop error, backing off:', error)
        dutySessionId = null
        inFlight = false
        await sleep(cfg.duty.backoffMs ?? 5000)
      }
    }
  }

  console.warn(`[dsh-walkie] duty loop starting, base=${cfg.base_url} token=${cfg.token ? 'yes' : 'no'}`)
  void loop()
}

// ── apply ────────────────────────────────────────────────────────────────────

export function apply(ctx) {
  const cfg = loadConfig()
  const client = makeClient(cfg)
  registerWalkieTools(ctx, client)
  if (cfg.duty.enabled !== false && cfg.token) {
    startDutyLoop(ctx, cfg, client)
  } else {
    console.warn('[dsh-walkie] duty loop disabled (no token or duty.enabled=false)')
  }
}
