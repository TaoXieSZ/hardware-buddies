import Foundation

public typealias VoiceAgentEventCallback = @Sendable (String) async -> Void
public typealias VoiceAgentRunEventCallback = @Sendable (VoiceAgentRunEvent) async -> Void

private struct SubAgentArgs: Sendable {
    var agentName: String?
    var systemPrompt: String
    var prompt: String

    static func parse(_ arguments: String) throws -> SubAgentArgs {
        let trimmed = arguments.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else {
            throw SubAgentArgumentError.missingPrompt(arguments)
        }

        let data = Data(trimmed.utf8)
        let value: Any
        do {
            value = try JSONSerialization.jsonObject(with: data, options: [.fragmentsAllowed])
        } catch {
            if trimmed.looksLikeJSON {
                throw SubAgentArgumentError.invalidJSON(trimmed, error.localizedDescription)
            }
            return SubAgentArgs(agentName: nil, systemPrompt: defaultSystemPrompt, prompt: trimmed)
        }

        if let prompt = value as? String, !prompt.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
            return SubAgentArgs(agentName: nil, systemPrompt: defaultSystemPrompt, prompt: prompt)
        }

        guard let object = value as? [String: Any] else {
            throw SubAgentArgumentError.unsupportedShape(trimmed)
        }

        let agentName = firstString(
            in: object,
            keys: ["agent", "agent_name", "agentName", "name"]
        )

        let systemPrompt = firstString(
            in: object,
            keys: ["system_prompt", "systemPrompt", "systemprompt", "system", "instructions"]
        ) ?? defaultSystemPrompt

        if let prompt = firstString(
            in: object,
            keys: ["prompt", "task", "input", "query", "question", "request", "user_prompt", "userPrompt"]
        ) {
            return SubAgentArgs(agentName: agentName, systemPrompt: systemPrompt, prompt: prompt)
        }

        if let onlyString = object.values.compactMap({ $0 as? String }).first(where: { !$0.isBlank }) {
            return SubAgentArgs(agentName: agentName, systemPrompt: systemPrompt, prompt: onlyString)
        }

        throw SubAgentArgumentError.missingPrompt(trimmed)
    }

    private static let defaultSystemPrompt = """
    You are an independent sub-agent. Complete the delegated task, keep your reasoning focused on the task, and return a concise result to the parent agent.
    """

    private static func firstString(in object: [String: Any], keys: [String]) -> String? {
        for key in keys {
            if let value = object[key] as? String, !value.isBlank {
                return value
            }
        }
        return nil
    }
}

private enum SubAgentArgumentError: LocalizedError {
    case invalidJSON(String, String)
    case unsupportedShape(String)
    case missingPrompt(String)

    var errorDescription: String? {
        switch self {
        case let .invalidJSON(arguments, underlying):
            "Invalid subagent arguments: malformed JSON \(preview(arguments)) (\(underlying))."
        case let .unsupportedShape(arguments):
            "Invalid subagent arguments: expected a JSON object or string, got \(preview(arguments))."
        case let .missingPrompt(arguments):
            "Invalid subagent arguments: missing non-empty prompt/task field in \(preview(arguments))."
        }
    }

    private func preview(_ value: String) -> String {
        let trimmed = value.trimmingCharacters(in: .whitespacesAndNewlines)
        if trimmed.count <= 160 { return "'\(trimmed)'" }
        return "'\(String(trimmed.prefix(160)))...'"
    }
}

private extension String {
    var isBlank: Bool {
        trimmingCharacters(in: .whitespacesAndNewlines).isEmpty
    }

    var looksLikeJSON: Bool {
        let trimmed = trimmingCharacters(in: .whitespacesAndNewlines)
        guard let first = trimmed.first else { return false }
        return first == "{" || first == "[" || first == "\""
    }

    var voiceAgentRunTitle: String {
        let compact = trimmingCharacters(in: .whitespacesAndNewlines)
            .replacingOccurrences(of: "\n", with: " ")
        guard compact.count > 80 else { return compact.isEmpty ? "Untitled run" : compact }
        return "\(String(compact.prefix(80)))..."
    }
}

/// Coda-style agentic runner: 由 LLM 自行决定何时调用工具（包括派生子 agent）。
/// subagent 只允许由 root agent 派生，用于真正可并行的独立子任务。
public actor VoiceAgentRunner {
    public static let maxDepth = 1
    public static let maxConcurrentCalls = 3
    public static let maxSubagentCallsPerRun = 4

    private let client: any VoiceAgentLLMClient
    private let model: String
    private let options: VoiceAgentOptions
    private let registeredTools: [any VoiceAgentTool]
    private let toolDefinitions: [OpenAIToolDefinition]
    private let toolLookup: [String: any VoiceAgentTool]
    private let sessionID: UUID
    private let agentName: String
    private let memory: VoiceAgentMemory
    private var namedSubAgents: [String: VoiceSubAgent] = [:]
    private let onEvent: VoiceAgentEventCallback?
    private let onRunEvent: VoiceAgentRunEventCallback?
    private let limiter: ConcurrencyLimiter
    private let runRegistry: VoiceAgentRunRegistry
    private var isRunning = false
    private var messages: [VoiceAgentMessage]

    public init(
        model: String,
        systemPrompt: String,
        client: any VoiceAgentLLMClient,
        tools: [any VoiceAgentTool] = [],
        options: VoiceAgentOptions = VoiceAgentOptions(),
        agentName: String = "root",
        onEvent: VoiceAgentEventCallback? = nil,
        onRunEvent: VoiceAgentRunEventCallback? = nil,
        runRegistry: VoiceAgentRunRegistry = VoiceAgentRunRegistry()
    ) {
        self.client = client
        self.model = model
        self.options = options
        self.registeredTools = tools
        self.toolDefinitions = tools.map { $0.openAIDefinition() }
        self.toolLookup = Dictionary(uniqueKeysWithValues: tools.map { ($0.name, $0) })
        self.sessionID = UUID()
        self.agentName = agentName
        self.memory = VoiceAgentMemory()
        self.onEvent = onEvent
        self.onRunEvent = onRunEvent
        self.limiter = ConcurrencyLimiter(limit: Self.maxConcurrentCalls)
        self.runRegistry = runRegistry
        self.messages = [.system(systemPrompt)]
    }

    /// Send user text and run the full agentic loop (tool calls + sub-agents).
    @discardableResult
    public func send(_ userText: String) async throws -> String {
        guard !isRunning else {
            throw VoiceAgentError.runAlreadyInProgress
        }
        isRunning = true
        defer { isRunning = false }

        var working = messages
        working.append(.user(userText))
        var remainingSubagentCalls = Self.maxSubagentCallsPerRun

        let rootRunID = UUID()
        let rootSnapshot = await runRegistry.startRun(
            runID: rootRunID,
            kind: .root,
            title: userText.voiceAgentRunTitle,
            parentRunID: nil,
            rootRunID: rootRunID,
            depth: 0,
            messages: working
        )
        await Self.emit(.runStarted(rootSnapshot), onEvent: onEvent, onRunEvent: onRunEvent)

        do {
            let result = try await Self.runAgent(
                messages: &working,
                model: model,
                client: client,
                toolDefinitions: toolDefinitions,
                toolLookup: toolLookup,
                namedSubAgents: namedSubAgents,
                options: options,
                runID: rootRunID,
                rootRunID: rootRunID,
                depth: 0,
                remainingSubagentCalls: &remainingSubagentCalls,
                sessionID: sessionID,
                agentName: agentName,
                memory: memory,
                limiter: limiter,
                runRegistry: runRegistry,
                onEvent: onEvent,
                onRunEvent: onRunEvent
            )
            messages = working
            await runRegistry.completeRun(rootRunID, output: result)
            await Self.emit(.runCompleted(runID: rootRunID, output: result), onEvent: onEvent, onRunEvent: onRunEvent)
            return result
        } catch {
            await runRegistry.failRun(rootRunID, error: error.localizedDescription)
            await Self.emit(.runFailed(runID: rootRunID, error: error.localizedDescription), onEvent: onEvent, onRunEvent: onRunEvent)
            throw error
        }
    }

    public func history() -> [VoiceAgentMessage] { messages }

    public func runSnapshots() async -> [VoiceAgentRunSnapshot] {
        await runRegistry.snapshots()
    }

    public func liveRuns() async -> [VoiceAgentRunSnapshot] {
        await runRegistry.snapshots()
    }

    public func runSnapshot(runID: UUID) async -> VoiceAgentRunSnapshot? {
        await runRegistry.snapshot(runID: runID)
    }

    public func reset() async {
        let sys = messages.first
        messages = sys.map { [$0] } ?? []
        await runRegistry.reset()
    }

    // MARK: - Named subagent registry

    public func registerSubAgent(_ subAgent: VoiceSubAgent) async {
        namedSubAgents[subAgent.name] = subAgent
    }

    public func availableSubAgentNames() -> [String] {
        namedSubAgents.keys.sorted()
    }

    // MARK: - Recursive agent loop

    private static func runAgent(
        messages: inout [VoiceAgentMessage],
        model: String,
        client: any VoiceAgentLLMClient,
        toolDefinitions: [OpenAIToolDefinition],
        toolLookup: [String: any VoiceAgentTool],
        namedSubAgents: [String: VoiceSubAgent],
        options: VoiceAgentOptions,
        runID: UUID,
        rootRunID: UUID,
        depth: Int,
        remainingSubagentCalls: inout Int,
        sessionID: UUID,
        agentName: String,
        memory: VoiceAgentMemory,
        limiter: ConcurrencyLimiter,
        runRegistry: VoiceAgentRunRegistry,
        onEvent: VoiceAgentEventCallback?,
        onRunEvent: VoiceAgentRunEventCallback?
    ) async throws -> String {
        while true {
            let allTools = depth == 0 && remainingSubagentCalls > 0
                ? toolDefinitions + [buildSubagentToolDefinition(namedAgents: namedSubAgents)]
                : toolDefinitions

            let request = OpenAIChatCompletionRequest(
                model: model,
                messages: messages,
                temperature: options.temperature,
                maxTokens: options.maxTokens,
                tools: allTools.isEmpty ? nil : allTools
            )

            // Acquire before API call, release after.
            await limiter.acquire()
            let response: VoiceAgentMessage
            do {
                response = try await client.complete(request)
            } catch {
                await limiter.release()
                throw error
            }
            await limiter.release()

            messages.append(response)
            await runRegistry.appendMessage(response, to: runID)
            await emit(.messageAppended(runID: runID, message: response), onEvent: onEvent, onRunEvent: onRunEvent)

            guard let toolCalls = response.toolCalls, !toolCalls.isEmpty else {
                return response.content
            }

            let allowedSubagentToolCallIDs = reserveSubagentToolCalls(
                toolCalls,
                remainingSubagentCalls: &remainingSubagentCalls
            )
            let requestedSubagentCount = toolCalls.filter { $0.function.name == "subagent" }.count
            if requestedSubagentCount > allowedSubagentToolCallIDs.count {
                await emit(
                    .notice(
                        runID: runID,
                        message: "[subagent] skipped \(requestedSubagentCount - allowedSubagentToolCallIDs.count) call(s): budget exhausted"
                    ),
                    onEvent: onEvent,
                    onRunEvent: onRunEvent
                )
            }

            // Execute all tool calls in parallel (bounded by limiter);
            // individual failures return error text instead of crashing the group.
            let toolResults: [(id: String, output: String)] = await withTaskGroup(
                of: (String, String).self
            ) { group in
                for toolCall in toolCalls {
                    let name = toolCall.function.name
                    let args = toolCall.function.arguments
                    let id = toolCall.id

                    group.addTask {
                        let startedToolCall = await runRegistry.startToolCall(
                            callID: id,
                            name: name,
                            arguments: args,
                            in: runID
                        )
                        await emit(.toolStarted(runID: runID, toolCall: startedToolCall), onEvent: onEvent, onRunEvent: onRunEvent)

                        do {
                            let output: String
                            var status: VoiceAgentToolCallStatus = .completed
                            if name == "subagent" {
                                if depth != 0 {
                                    output = "Error: subagent delegation is only available to the root agent. Complete this task directly instead of delegating again."
                                    status = .skipped
                                } else if !allowedSubagentToolCallIDs.contains(id) {
                                    output = "Error: subagent budget exceeded. Use the existing context and completed subagent results to synthesize the answer."
                                    status = .skipped
                                } else {
                                    output = try await handleSubagent(
                                        args,
                                        model: model,
                                        client: client,
                                        toolDefinitions: toolDefinitions,
                                        toolLookup: toolLookup,
                                        namedSubAgents: namedSubAgents,
                                        options: options,
                                        parentRunID: runID,
                                        rootRunID: rootRunID,
                                        depth: depth,
                                        sessionID: sessionID,
                                        agentName: agentName,
                                        memory: memory,
                                        limiter: limiter,
                                        runRegistry: runRegistry,
                                        onEvent: onEvent,
                                        onRunEvent: onRunEvent
                                    )
                                }
                            } else if let tool = toolLookup[name] {
                                let memorySnapshot = await memory.snapshot()
                                let ctx = VoiceAgentToolContext(
                                    sessionID: sessionID,
                                    agentName: agentName,
                                    memory: memorySnapshot
                                )
                                output = try await tool.call(input: args, context: ctx)
                            } else {
                                output = "Error: unknown tool '\(name)'"
                                status = .failed
                            }
                            if let finishedToolCall = await runRegistry.finishToolCall(
                                callID: id,
                                in: runID,
                                status: status,
                                output: output,
                                error: status == .failed ? output : nil
                            ) {
                                await emit(
                                    .toolFinished(runID: runID, toolCall: finishedToolCall),
                                    onEvent: onEvent,
                                    onRunEvent: onRunEvent
                                )
                            }
                            return (id, output)
                        } catch {
                            let output = "Error: \(error.localizedDescription)"
                            if let finishedToolCall = await runRegistry.finishToolCall(
                                callID: id,
                                in: runID,
                                status: .failed,
                                output: output,
                                error: error.localizedDescription
                            ) {
                                await emit(
                                    .toolFinished(runID: runID, toolCall: finishedToolCall),
                                    onEvent: onEvent,
                                    onRunEvent: onRunEvent
                                )
                            }
                            return (id, output)
                        }
                    }
                }
                var results: [(String, String)] = []
                for await r in group { results.append(r) }
                return results
            }

            // Append results in original tool_calls order
            for toolCall in toolCalls {
                if let r = toolResults.first(where: { $0.id == toolCall.id }) {
                    let toolMessage = VoiceAgentMessage.tool(r.output, toolCallID: r.id)
                    messages.append(toolMessage)
                    await runRegistry.appendMessage(toolMessage, to: runID)
                    await emit(.messageAppended(runID: runID, message: toolMessage), onEvent: onEvent, onRunEvent: onRunEvent)
                }
            }
        }
    }

    private static func reserveSubagentToolCalls(
        _ toolCalls: [OpenAIToolCall],
        remainingSubagentCalls: inout Int
    ) -> Set<String> {
        guard remainingSubagentCalls > 0 else { return [] }

        let requested = toolCalls.filter { $0.function.name == "subagent" }
        let allowed = requested.prefix(remainingSubagentCalls).map(\.id)
        remainingSubagentCalls -= allowed.count
        return Set(allowed)
    }

    private static func handleSubagent(
        _ arguments: String,
        model: String,
        client: any VoiceAgentLLMClient,
        toolDefinitions: [OpenAIToolDefinition],
        toolLookup: [String: any VoiceAgentTool],
        namedSubAgents: [String: VoiceSubAgent],
        options: VoiceAgentOptions,
        parentRunID: UUID,
        rootRunID: UUID,
        depth: Int,
        sessionID: UUID,
        agentName: String,
        memory: VoiceAgentMemory,
        limiter: ConcurrencyLimiter,
        runRegistry: VoiceAgentRunRegistry,
        onEvent: VoiceAgentEventCallback?,
        onRunEvent: VoiceAgentRunEventCallback?
    ) async throws -> String {
        guard depth + 1 <= maxDepth else {
            return "Error: max sub-agent depth (\(maxDepth)) exceeded"
        }

        let parsed = try SubAgentArgs.parse(arguments)

        // Named subagent path: agentic loop with the agent's own tools + memory
        if let requestedName = parsed.agentName,
           let namedAgent = namedSubAgents[requestedName] {
            let agentTools = await namedAgent.registeredTools()
            let agentMemory = await namedAgent.agentMemory()
            let agentClient = await namedAgent.agentLLMClient()
            let agentToolDefs = agentTools.map { $0.openAIDefinition() }
            let agentToolLookup = Dictionary(uniqueKeysWithValues: agentTools.map { ($0.name, $0) })

            // Preserve the specialist's own instructions, then add memory context.
            let memorySnapshot = await agentMemory.snapshot()
            let systemPromptText: String
            if memorySnapshot.notes.isEmpty && memorySnapshot.facts.isEmpty {
                systemPromptText = namedAgent.systemPrompt
            } else {
                systemPromptText = """
                \(namedAgent.systemPrompt)

                # 记忆

                \(memorySnapshot.rendered)
                """
            }

            var subMessages: [VoiceAgentMessage] = [
                .system(systemPromptText),
                .user(parsed.prompt),
            ]
            let childRunID = UUID()
            let childSnapshot = await runRegistry.startRun(
                runID: childRunID,
                kind: .subagent,
                title: "[\(requestedName)] \(parsed.prompt.voiceAgentRunTitle)",
                parentRunID: parentRunID,
                rootRunID: rootRunID,
                depth: depth + 1,
                messages: subMessages
            )
            await emit(.runStarted(childSnapshot), onEvent: onEvent, onRunEvent: onRunEvent)

            var childSubagentBudget = 0
            do {
                let output = try await runAgent(
                    messages: &subMessages,
                    model: namedAgent.model,
                    client: agentClient,
                    toolDefinitions: agentToolDefs,
                    toolLookup: agentToolLookup,
                    namedSubAgents: [:],
                    options: namedAgent.options,
                    runID: childRunID,
                    rootRunID: rootRunID,
                    depth: depth + 1,
                    remainingSubagentCalls: &childSubagentBudget,
                    sessionID: sessionID,
                    agentName: requestedName,
                    memory: agentMemory,
                    limiter: limiter,
                    runRegistry: runRegistry,
                    onEvent: onEvent,
                    onRunEvent: onRunEvent
                )
                // Persist result in agent's memory for future delegations
                await namedAgent.remember("Task: \(parsed.prompt)\nResult: \(output)")
                await runRegistry.completeRun(childRunID, output: output)
                await emit(.runCompleted(runID: childRunID, output: output), onEvent: onEvent, onRunEvent: onRunEvent)
                return output
            } catch {
                await runRegistry.failRun(childRunID, error: error.localizedDescription)
                await emit(.runFailed(runID: childRunID, error: error.localizedDescription), onEvent: onEvent, onRunEvent: onRunEvent)
                throw error
            }
        }

        // Anonymous subagent path: recursive runAgent (original behavior)
        var subMessages: [VoiceAgentMessage] = [
            .system(parsed.systemPrompt),
            .user(parsed.prompt),
        ]
        let childRunID = UUID()
        let childSnapshot = await runRegistry.startRun(
            runID: childRunID,
            kind: .subagent,
            title: parsed.prompt.voiceAgentRunTitle,
            parentRunID: parentRunID,
            rootRunID: rootRunID,
            depth: depth + 1,
            messages: subMessages
        )
        await emit(.runStarted(childSnapshot), onEvent: onEvent, onRunEvent: onRunEvent)

        var childSubagentBudget = 0
        do {
            let output = try await runAgent(
                messages: &subMessages,
                model: model,
                client: client,
                toolDefinitions: toolDefinitions,
                toolLookup: toolLookup,
                namedSubAgents: [:],
                options: options,
                runID: childRunID,
                rootRunID: rootRunID,
                depth: depth + 1,
                remainingSubagentCalls: &childSubagentBudget,
                sessionID: sessionID,
                agentName: agentName,
                memory: memory,
                limiter: limiter,
                runRegistry: runRegistry,
                onEvent: onEvent,
                onRunEvent: onRunEvent
            )
            await runRegistry.completeRun(childRunID, output: output)
            await emit(.runCompleted(runID: childRunID, output: output), onEvent: onEvent, onRunEvent: onRunEvent)
            return output
        } catch {
            await runRegistry.failRun(childRunID, error: error.localizedDescription)
            await emit(.runFailed(runID: childRunID, error: error.localizedDescription), onEvent: onEvent, onRunEvent: onRunEvent)
            throw error
        }
    }

    private static func emit(
        _ event: VoiceAgentRunEvent,
        onEvent: VoiceAgentEventCallback?,
        onRunEvent: VoiceAgentRunEventCallback?
    ) async {
        await onRunEvent?(event)
        await onEvent?(event.displayText)
    }

    private static func buildSubagentToolDefinition(namedAgents: [String: VoiceSubAgent]) -> OpenAIToolDefinition {
        var agentDescription = "Optional name of a registered specialist sub-agent to handle this task. If omitted, an anonymous general-purpose sub-agent is used."
        if !namedAgents.isEmpty {
            let roster = namedAgents.keys.sorted().map { name -> String in
                let purpose = namedAgents[name].map { "— \($0.purpose)" } ?? ""
                return "  - \"\(name)\" \(purpose)"
            }.joined(separator: "\n")
            agentDescription += "\n\nAvailable agents:\n\(roster)"
        }

        return OpenAIToolDefinition(
            function: .init(
                name: "subagent",
                description: "Launch an independent sub-agent for a self-contained subtask only when parallel delegation is materially better than doing the work serially in the current agent. Do not use this for trivial lookups, follow-up synthesis, formatting, or tasks whose output depends on another subtask. For complex requests, split into at most 3-4 independent subagents total. Subagents cannot delegate again; they must complete their assigned task directly.",
                parameters: .object([
                    "type": .string("object"),
                    "properties": .object([
                        "agent": .object([
                            "type": .string("string"),
                            "description": .string(agentDescription),
                        ]),
                        "system_prompt": .object([
                            "type": .string("string"),
                            "description": .string("Optional system prompt for the sub-agent to follow. Ignored when using a named agent (they have their own). If omitted for anonymous agents, a safe default is used."),
                        ]),
                        "prompt": .object([
                            "type": .string("string"),
                            "description": .string("The independent task for the sub-agent to complete directly. Make it self-contained and include the needed context; do not ask it to create more subagents."),
                        ]),
                    ]),
                    "required": .array([.string("prompt")]),
                ])
            )
        )
    }
}

public extension VoiceAgentRunner {
    static func configuredOpenAI(
        systemPrompt: String,
        tools: [any VoiceAgentTool] = [],
        options: VoiceAgentOptions = VoiceAgentOptions(),
        onEvent: VoiceAgentEventCallback? = nil,
        onRunEvent: VoiceAgentRunEventCallback? = nil
    ) -> VoiceAgentRunner {
        VoiceAgentRunner(
            model: VoiceAgentRuntimeConfig.openAIModel,
            systemPrompt: systemPrompt,
            client: OpenAICompatibleChatClient.configuredOpenAI(),
            tools: tools,
            options: options,
            onEvent: onEvent,
            onRunEvent: onRunEvent
        )
    }
}
