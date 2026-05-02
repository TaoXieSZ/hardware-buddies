import Foundation

// MARK: - Assignment / Events / Result

public struct VoiceSubAgentAssignment: Equatable, Sendable {
    public var agentName: String
    public var task: String
    public var context: String?
    public var toolInvocations: [VoiceAgentToolInvocation]

    public init(
        agentName: String,
        task: String,
        context: String? = nil,
        toolInvocations: [VoiceAgentToolInvocation] = []
    ) {
        self.agentName = agentName
        self.task = task
        self.context = context
        self.toolInvocations = toolInvocations
    }
}

public enum VoiceSubAgentEventPhase: String, Equatable, Sendable {
    case started
    case toolStarted
    case toolFinished
    case completed
    case failed
}

public struct VoiceSubAgentEvent: Equatable, Sendable {
    public var runID: UUID
    public var agentName: String
    public var task: String
    public var phase: VoiceSubAgentEventPhase
    public var toolName: String?
    public var message: String?
    public var timestamp: Date

    public init(
        runID: UUID,
        agentName: String,
        task: String,
        phase: VoiceSubAgentEventPhase,
        toolName: String? = nil,
        message: String? = nil,
        timestamp: Date = Date()
    ) {
        self.runID = runID
        self.agentName = agentName
        self.task = task
        self.phase = phase
        self.toolName = toolName
        self.message = message
        self.timestamp = timestamp
    }
}

public typealias VoiceSubAgentEventHandler = @Sendable (VoiceSubAgentEvent) async -> Void

public struct VoiceSubAgentResult: Equatable, Sendable {
    public var runID: UUID
    public var agentName: String
    public var task: String
    public var output: String
    public var toolResults: [VoiceAgentToolResult]
    public var memory: VoiceAgentMemorySnapshot
    public var session: VoiceAgentSessionSnapshot
    public var startedAt: Date
    public var completedAt: Date
}

public enum VoiceSubAgentRunStatus: Equatable, Sendable {
    case running
    case completed(VoiceSubAgentResult)
    case failed(String)
    case cancelled
}

public struct VoiceSubAgentLiveSnapshot: Sendable {
    public var runID: UUID
    public var agentName: String
    public var task: String
    public var spawnedAt: Date
    public var status: VoiceSubAgentRunStatus
    public var session: VoiceAgentSessionSnapshot
    public var memory: VoiceAgentMemorySnapshot
}

// MARK: - Sub-agent

/// 一个有名字、有用途、可携带 memory 与命名工具集的子 agent。
/// 由 `VoiceAgentOrchestrator` 在结构化任务流中按名调度。
public actor VoiceSubAgent {
    public let name: String
    public let purpose: String
    public let model: String
    public let options: VoiceAgentOptions
    public let systemPrompt: String

    private let agent: VoiceAgent
    private let llmClient: any VoiceAgentLLMClient
    private let memory: VoiceAgentMemory
    private let tools: [String: any VoiceAgentTool]
    private let eventHandler: VoiceSubAgentEventHandler?

    public init(
        name: String,
        purpose: String,
        model: String,
        systemPrompt: String,
        options: VoiceAgentOptions = VoiceAgentOptions(),
        client: any VoiceAgentLLMClient,
        memory: VoiceAgentMemory = VoiceAgentMemory(),
        tools: [any VoiceAgentTool] = [],
        eventHandler: VoiceSubAgentEventHandler? = nil
    ) {
        self.name = name
        self.purpose = purpose
        self.model = model
        self.options = options
        self.systemPrompt = systemPrompt
        self.llmClient = client
        self.agent = VoiceAgent(
            model: model,
            systemPrompt: systemPrompt,
            options: options,
            client: client
        )
        self.memory = memory
        self.tools = Dictionary(uniqueKeysWithValues: tools.map { ($0.name, $0) })
        self.eventHandler = eventHandler
    }

    public func run(_ assignment: VoiceSubAgentAssignment) async throws -> VoiceSubAgentResult {
        let runID = UUID()
        let startedAt = Date()
        await emit(
            runID: runID,
            task: assignment.task,
            phase: .started,
            message: "Subagent \(name) started."
        )

        do {
            let toolResults = try await executeTools(
                assignment.toolInvocations,
                runID: runID,
                task: assignment.task
            )
            for result in toolResults {
                await memory.remember("Tool \(result.name) returned: \(result.output)")
            }

            let memorySnapshot = await memory.snapshot()
            let prompt = Self.buildPrompt(
                agentName: name,
                purpose: purpose,
                task: assignment.task,
                context: assignment.context,
                memory: memorySnapshot,
                tools: availableToolDescriptions(),
                toolResults: toolResults
            )

            let output = try await agent.send(prompt)
            await memory.remember("Task: \(assignment.task)\nAnswer: \(output)")

            let completedAt = Date()
            await emit(
                runID: runID,
                task: assignment.task,
                phase: .completed,
                message: "Subagent \(name) completed."
            )

            return VoiceSubAgentResult(
                runID: runID,
                agentName: name,
                task: assignment.task,
                output: output,
                toolResults: toolResults,
                memory: await memory.snapshot(),
                session: await agent.snapshot(),
                startedAt: startedAt,
                completedAt: completedAt
            )
        } catch {
            await emit(
                runID: runID,
                task: assignment.task,
                phase: .failed,
                message: error.localizedDescription
            )
            throw error
        }
    }

    public func executeTool(_ invocation: VoiceAgentToolInvocation) async throws -> VoiceAgentToolResult {
        let runID = UUID()
        return try await executeTools([invocation], runID: runID, task: "manual tool execution").first!
    }

    public func memorySnapshot() async -> VoiceAgentMemorySnapshot {
        await memory.snapshot()
    }

    public func sessionSnapshot() async -> VoiceAgentSessionSnapshot {
        await agent.snapshot()
    }

    public func reset(keepingSystemPrompt: Bool = true, clearingMemory: Bool = false) async {
        await agent.reset(keepingSystemPrompt: keepingSystemPrompt)
        if clearingMemory {
            await memory.reset()
        }
    }

    public func availableTools() -> [String] {
        tools.keys.sorted()
    }

    // MARK: - Runner integration

    /// All registered tools, for VoiceAgentRunner agentic loop integration.
    public func registeredTools() -> [any VoiceAgentTool] {
        Array(tools.values)
    }

    /// The underlying memory actor, for VoiceAgentRunner tool context construction.
    public func agentMemory() -> VoiceAgentMemory {
        memory
    }

    /// The LLM client configured for this specialist.
    public func agentLLMClient() -> any VoiceAgentLLMClient {
        llmClient
    }

    /// Record a note in this agent's memory (used by runner after completing a task).
    public func remember(_ note: String) async {
        await memory.remember(note)
    }

    private func executeTools(
        _ invocations: [VoiceAgentToolInvocation],
        runID: UUID,
        task: String
    ) async throws -> [VoiceAgentToolResult] {
        guard !invocations.isEmpty else { return [] }

        let memorySnapshot = await memory.snapshot()
        let session = await agent.snapshot()
        let context = VoiceAgentToolContext(
            sessionID: session.sessionID,
            agentName: name,
            memory: memorySnapshot
        )

        var results: [VoiceAgentToolResult] = []
        results.reserveCapacity(invocations.count)
        for invocation in invocations {
            guard let tool = tools[invocation.name] else {
                throw VoiceAgentError.unknownTool(agentName: name, toolName: invocation.name)
            }
            await emit(
                runID: runID,
                task: task,
                phase: .toolStarted,
                toolName: invocation.name,
                message: "Tool \(invocation.name) started."
            )
            let output = try await tool.call(input: invocation.input, context: context)
            results.append(
                VoiceAgentToolResult(
                    name: invocation.name,
                    input: invocation.input,
                    output: output
                )
            )
            await emit(
                runID: runID,
                task: task,
                phase: .toolFinished,
                toolName: invocation.name,
                message: "Tool \(invocation.name) finished."
            )
        }
        return results
    }

    private func emit(
        runID: UUID,
        task: String,
        phase: VoiceSubAgentEventPhase,
        toolName: String? = nil,
        message: String? = nil
    ) async {
        guard let eventHandler else { return }
        await eventHandler(
            VoiceSubAgentEvent(
                runID: runID,
                agentName: name,
                task: task,
                phase: phase,
                toolName: toolName,
                message: message
            )
        )
    }

    private func availableToolDescriptions() -> [String] {
        tools.values
            .sorted { $0.name < $1.name }
            .map { "\($0.name): \($0.description)" }
    }

    private static func buildPrompt(
        agentName: String,
        purpose: String,
        task: String,
        context: String?,
        memory: VoiceAgentMemorySnapshot,
        tools: [String],
        toolResults: [VoiceAgentToolResult]
    ) -> String {
        var sections: [String] = [
            "Subagent: \(agentName)",
            "Purpose: \(purpose)",
            "Task:\n\(task)",
        ]

        if let context, !context.isEmpty {
            sections.append("Shared context:\n\(context)")
        }

        sections.append("Memory:\n\(memory.rendered)")

        if !tools.isEmpty {
            sections.append("Available tools:\n" + tools.map { "- \($0)" }.joined(separator: "\n"))
        }

        if !toolResults.isEmpty {
            let renderedResults = toolResults
                .map { "- \($0.name)(\($0.input)) -> \($0.output)" }
                .joined(separator: "\n")
            sections.append("Tool results:\n\(renderedResults)")
        }

        sections.append("Return your result for the supervisor. Be concise and include relevant evidence from memory or tools.")
        return sections.joined(separator: "\n\n")
    }
}

// MARK: - Run handle (tokio::JoinHandle 等价)

/// `VoiceSubAgentHandle` 是 `tokio::JoinHandle` 的 Swift 对应：
/// 句柄返回时底层 `Task` 已并发开始运行；orchestrator 在 supervisor 整个生命周期内
/// 持有句柄，使其 session/memory 在任务完成后仍可被 supervisor 检视。
public actor VoiceSubAgentHandle {
    public nonisolated let runID: UUID
    public nonisolated let agentName: String
    public nonisolated let task: String
    public nonisolated let spawnedAt: Date

    private let subAgent: VoiceSubAgent
    private let runTask: Task<VoiceSubAgentResult, Error>
    private var resolved: VoiceSubAgentRunStatus = .running

    init(
        runID: UUID,
        agentName: String,
        task: String,
        spawnedAt: Date,
        subAgent: VoiceSubAgent,
        runTask: Task<VoiceSubAgentResult, Error>
    ) {
        self.runID = runID
        self.agentName = agentName
        self.task = task
        self.spawnedAt = spawnedAt
        self.subAgent = subAgent
        self.runTask = runTask
    }

    @discardableResult
    public func value() async throws -> VoiceSubAgentResult {
        do {
            let result = try await runTask.value
            resolved = .completed(result)
            return result
        } catch is CancellationError {
            resolved = .cancelled
            throw CancellationError()
        } catch {
            resolved = .failed(error.localizedDescription)
            throw error
        }
    }

    public func cancel() {
        runTask.cancel()
    }

    public func status() -> VoiceSubAgentRunStatus { resolved }

    public func sessionSnapshot() async -> VoiceAgentSessionSnapshot {
        await subAgent.sessionSnapshot()
    }

    public func memorySnapshot() async -> VoiceAgentMemorySnapshot {
        await subAgent.memorySnapshot()
    }

    public func liveSnapshot() async -> VoiceSubAgentLiveSnapshot {
        VoiceSubAgentLiveSnapshot(
            runID: runID,
            agentName: agentName,
            task: task,
            spawnedAt: spawnedAt,
            status: resolved,
            session: await subAgent.sessionSnapshot(),
            memory: await subAgent.memorySnapshot()
        )
    }
}
