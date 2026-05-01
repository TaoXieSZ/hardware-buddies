import Foundation

public enum VoiceAgentHardcodedConfig {
    public static let openAIBaseURL = URL(string: "https://openrouter.ai/api/v1")!
    public static let openAIAPIKey = "REMOVED_API_KEY"
    public static let defaultModel = "deepseek/deepseek-v4-flash"
}

public enum VoiceAgentRole: String, Codable, Sendable {
    case system
    case user
    case assistant
    case tool
}

public struct VoiceAgentMessage: Codable, Equatable, Sendable {
    public var role: VoiceAgentRole
    public var content: String
    public var name: String?
    public var toolCallID: String?

    public init(
        role: VoiceAgentRole,
        content: String,
        name: String? = nil,
        toolCallID: String? = nil
    ) {
        self.role = role
        self.content = content
        self.name = name
        self.toolCallID = toolCallID
    }

    public static func system(_ content: String) -> VoiceAgentMessage {
        VoiceAgentMessage(role: .system, content: content)
    }

    public static func user(_ content: String) -> VoiceAgentMessage {
        VoiceAgentMessage(role: .user, content: content)
    }

    public static func assistant(_ content: String) -> VoiceAgentMessage {
        VoiceAgentMessage(role: .assistant, content: content)
    }

    public static func tool(_ content: String, toolCallID: String) -> VoiceAgentMessage {
        VoiceAgentMessage(role: .tool, content: content, toolCallID: toolCallID)
    }

    private enum CodingKeys: String, CodingKey {
        case role
        case content
        case name
        case toolCallID = "tool_call_id"
    }
}

public struct OpenAIChatCompletionRequest: Codable, Sendable {
    public var model: String
    public var messages: [VoiceAgentMessage]
    public var temperature: Double?
    public var maxTokens: Int?
    public var stream: Bool?

    public init(
        model: String,
        messages: [VoiceAgentMessage],
        temperature: Double? = nil,
        maxTokens: Int? = nil,
        stream: Bool? = nil
    ) {
        self.model = model
        self.messages = messages
        self.temperature = temperature
        self.maxTokens = maxTokens
        self.stream = stream
    }

    private enum CodingKeys: String, CodingKey {
        case model
        case messages
        case temperature
        case maxTokens = "max_tokens"
        case stream
    }
}

public struct OpenAIChatCompletionResponse: Codable, Sendable {
    public struct Choice: Codable, Sendable {
        public var index: Int
        public var message: VoiceAgentMessage
        public var finishReason: String?

        private enum CodingKeys: String, CodingKey {
            case index
            case message
            case finishReason = "finish_reason"
        }
    }

    public struct Usage: Codable, Sendable {
        public var promptTokens: Int?
        public var completionTokens: Int?
        public var totalTokens: Int?

        private enum CodingKeys: String, CodingKey {
            case promptTokens = "prompt_tokens"
            case completionTokens = "completion_tokens"
            case totalTokens = "total_tokens"
        }
    }

    public var id: String?
    public var object: String?
    public var created: Int?
    public var model: String?
    public var choices: [Choice]
    public var usage: Usage?
}

public struct VoiceAgentTurn: Equatable, Sendable {
    public var sessionID: UUID
    public var index: Int
    public var userMessage: VoiceAgentMessage
    public var assistantMessage: VoiceAgentMessage
}

public struct VoiceAgentSessionSnapshot: Equatable, Sendable {
    public var sessionID: UUID
    public var model: String
    public var createdAt: Date
    public var updatedAt: Date
    public var messages: [VoiceAgentMessage]

    public var turnCount: Int {
        messages.filter { $0.role == .user }.count
    }
}

public struct VoiceAgentMemorySnapshot: Equatable, Sendable {
    public var facts: [String: String]
    public var notes: [String]

    public var rendered: String {
        var lines: [String] = []
        if !facts.isEmpty {
            lines.append("Facts:")
            for key in facts.keys.sorted() {
                lines.append("- \(key): \(facts[key] ?? "")")
            }
        }
        if !notes.isEmpty {
            lines.append("Notes:")
            for note in notes {
                lines.append("- \(note)")
            }
        }
        return lines.isEmpty ? "No memory yet." : lines.joined(separator: "\n")
    }
}

public actor VoiceAgentMemory {
    private var facts: [String: String]
    private var notes: [String]

    public init(facts: [String: String] = [:], notes: [String] = []) {
        self.facts = facts
        self.notes = notes
    }

    public func setFact(_ key: String, value: String) {
        facts[key] = value
    }

    public func fact(_ key: String) -> String? {
        facts[key]
    }

    public func remember(_ note: String) {
        notes.append(note)
    }

    public func snapshot() -> VoiceAgentMemorySnapshot {
        VoiceAgentMemorySnapshot(facts: facts, notes: notes)
    }

    public func reset() {
        facts = [:]
        notes = []
    }
}

public struct VoiceAgentToolContext: Sendable {
    public var sessionID: UUID
    public var agentName: String
    public var memory: VoiceAgentMemorySnapshot

    public init(sessionID: UUID, agentName: String, memory: VoiceAgentMemorySnapshot) {
        self.sessionID = sessionID
        self.agentName = agentName
        self.memory = memory
    }
}

public struct VoiceAgentToolInvocation: Equatable, Sendable {
    public var name: String
    public var input: String

    public init(name: String, input: String) {
        self.name = name
        self.input = input
    }
}

public struct VoiceAgentToolResult: Equatable, Sendable {
    public var name: String
    public var input: String
    public var output: String

    public init(name: String, input: String, output: String) {
        self.name = name
        self.input = input
        self.output = output
    }
}

public protocol VoiceAgentTool: Sendable {
    var name: String { get }
    var description: String { get }

    func call(input: String, context: VoiceAgentToolContext) async throws -> String
}

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

public struct VoiceAgentOrchestrationResult: Equatable, Sendable {
    public var input: String
    public var finalOutput: String
    public var subAgentResults: [VoiceSubAgentResult]
    public var supervisorSession: VoiceAgentSessionSnapshot
}

public enum VoiceAgentError: Error, LocalizedError, Equatable {
    case missingAPIKey
    case invalidEndpoint(URL)
    case emptyResponse
    case badStatusCode(Int, String)
    case unknownSubAgent(String)
    case unknownTool(agentName: String, toolName: String)

    public var errorDescription: String? {
        switch self {
        case .missingAPIKey:
            "Missing OpenAI-compatible API key."
        case let .invalidEndpoint(url):
            "Invalid OpenAI-compatible endpoint: \(url.absoluteString)"
        case .emptyResponse:
            "The model returned no assistant message."
        case let .badStatusCode(code, body):
            "OpenAI-compatible endpoint returned HTTP \(code): \(body)"
        case let .unknownSubAgent(name):
            "Unknown subagent: \(name)"
        case let .unknownTool(agentName, toolName):
            "Unknown tool '\(toolName)' for subagent '\(agentName)'."
        }
    }
}

public protocol VoiceAgentLLMClient: Sendable {
    func complete(_ request: OpenAIChatCompletionRequest) async throws -> VoiceAgentMessage
}

public final class OpenAICompatibleChatClient: VoiceAgentLLMClient, @unchecked Sendable {
    private let endpoint: URL
    private let apiKeyProvider: @Sendable () -> String?
    private let additionalHeaders: [String: String]
    private let session: URLSession
    private let encoder: JSONEncoder
    private let decoder: JSONDecoder

    public init(
        baseURL: URL = URL(string: "https://openrouter.ai/api/v1")!,
        apiKeyProvider: @escaping @Sendable () -> String?,
        additionalHeaders: [String: String] = [:],
        session: URLSession = .shared
    ) {
        self.endpoint = baseURL.appendingPathComponent("chat/completions")
        self.apiKeyProvider = apiKeyProvider
        self.additionalHeaders = additionalHeaders
        self.session = session
        self.encoder = JSONEncoder()
        self.decoder = JSONDecoder()
    }

    public func complete(_ request: OpenAIChatCompletionRequest) async throws -> VoiceAgentMessage {
        guard let apiKey = apiKeyProvider(), !apiKey.isEmpty else {
            throw VoiceAgentError.missingAPIKey
        }

        var urlRequest = URLRequest(url: endpoint)
        urlRequest.httpMethod = "POST"
        urlRequest.setValue("application/json", forHTTPHeaderField: "Content-Type")
        urlRequest.setValue("Bearer \(apiKey)", forHTTPHeaderField: "Authorization")
        for (key, value) in additionalHeaders {
            urlRequest.setValue(value, forHTTPHeaderField: key)
        }
        urlRequest.httpBody = try encoder.encode(request)

        let (data, response) = try await session.data(for: urlRequest)
        guard let httpResponse = response as? HTTPURLResponse else {
            throw VoiceAgentError.invalidEndpoint(endpoint)
        }
        guard (200 ..< 300).contains(httpResponse.statusCode) else {
            let body = String(data: data, encoding: .utf8) ?? ""
            throw VoiceAgentError.badStatusCode(httpResponse.statusCode, body)
        }

        let completion = try decoder.decode(OpenAIChatCompletionResponse.self, from: data)
        guard let message = completion.choices.first?.message else {
            throw VoiceAgentError.emptyResponse
        }
        return message
    }
}

public extension OpenAICompatibleChatClient {
    static func hardcodedOpenAI() -> OpenAICompatibleChatClient {
        OpenAICompatibleChatClient(
            baseURL: VoiceAgentHardcodedConfig.openAIBaseURL,
            apiKeyProvider: {
                VoiceAgentHardcodedConfig.openAIAPIKey
            }
        )
    }
}

public struct VoiceAgentOptions: Equatable, Sendable {
    public var temperature: Double?
    public var maxTokens: Int?

    public init(temperature: Double? = nil, maxTokens: Int? = nil) {
        self.temperature = temperature
        self.maxTokens = maxTokens
    }
}

public actor VoiceAgent {
    private let sessionID: UUID
    private let model: String
    private let options: VoiceAgentOptions
    private let client: any VoiceAgentLLMClient
    private let initialSystemPrompt: String?
    private let createdAt: Date
    private var updatedAt: Date
    private var messages: [VoiceAgentMessage]

    public init(
        sessionID: UUID = UUID(),
        model: String,
        systemPrompt: String? = nil,
        options: VoiceAgentOptions = VoiceAgentOptions(),
        client: any VoiceAgentLLMClient
    ) {
        self.sessionID = sessionID
        self.model = model
        self.options = options
        self.client = client
        self.initialSystemPrompt = systemPrompt
        self.createdAt = Date()
        self.updatedAt = createdAt
        self.messages = systemPrompt.map { [.system($0)] } ?? []
    }

    @discardableResult
    public func send(_ userText: String) async throws -> String {
        let turn = try await sendTurn(userText)
        return turn.assistantMessage.content
    }

    @discardableResult
    public func sendTurn(_ userText: String) async throws -> VoiceAgentTurn {
        let userMessage = VoiceAgentMessage.user(userText)
        messages.append(userMessage)

        let request = OpenAIChatCompletionRequest(
            model: model,
            messages: messages,
            temperature: options.temperature,
            maxTokens: options.maxTokens,
            stream: false
        )

        do {
            let assistantMessage = try await client.complete(request)
            messages.append(assistantMessage)
            updatedAt = Date()
            return VoiceAgentTurn(
                sessionID: sessionID,
                index: messages.filter { $0.role == .user }.count,
                userMessage: userMessage,
                assistantMessage: assistantMessage
            )
        } catch {
            if messages.last == userMessage {
                messages.removeLast()
            }
            throw error
        }
    }

    public func history() -> [VoiceAgentMessage] {
        messages
    }

    public func snapshot() -> VoiceAgentSessionSnapshot {
        VoiceAgentSessionSnapshot(
            sessionID: sessionID,
            model: model,
            createdAt: createdAt,
            updatedAt: updatedAt,
            messages: messages
        )
    }

    public func reset(keepingSystemPrompt: Bool = true) {
        if keepingSystemPrompt, let initialSystemPrompt {
            messages = [.system(initialSystemPrompt)]
        } else {
            messages = []
        }
        updatedAt = Date()
    }
}

public extension VoiceAgent {
    static func hardcodedOpenAI(
        model: String = VoiceAgentHardcodedConfig.defaultModel,
        systemPrompt: String? = nil,
        options: VoiceAgentOptions = VoiceAgentOptions()
    ) -> VoiceAgent {
        VoiceAgent(
            model: model,
            systemPrompt: systemPrompt,
            options: options,
            client: OpenAICompatibleChatClient.hardcodedOpenAI()
        )
    }
}

public actor VoiceSubAgent {
    public let name: String
    public let purpose: String

    private let agent: VoiceAgent
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

public actor VoiceAgentOrchestrator {
    private let supervisor: VoiceAgent
    private var subAgents: [String: VoiceSubAgent]

    public init(supervisor: VoiceAgent, subAgents: [VoiceSubAgent]) {
        self.supervisor = supervisor
        self.subAgents = Dictionary(uniqueKeysWithValues: subAgents.map { ($0.name, $0) })
    }

    public func addSubAgent(_ subAgent: VoiceSubAgent) {
        subAgents[subAgent.name] = subAgent
    }

    public func availableSubAgents() -> [String] {
        subAgents.keys.sorted()
    }

    public func run(_ input: String) async throws -> VoiceAgentOrchestrationResult {
        let assignments = subAgents.keys.sorted().map {
            VoiceSubAgentAssignment(agentName: $0, task: input)
        }
        return try await run(input, assignments: assignments)
    }

    public func run(
        _ input: String,
        assignments: [VoiceSubAgentAssignment]
    ) async throws -> VoiceAgentOrchestrationResult {
        let results = try await runSubAgents(assignments)
            .sorted { $0.agentName < $1.agentName }

        let finalPrompt = Self.buildSupervisorPrompt(input: input, results: results)
        let finalOutput = try await supervisor.send(finalPrompt)
        return VoiceAgentOrchestrationResult(
            input: input,
            finalOutput: finalOutput,
            subAgentResults: results,
            supervisorSession: await supervisor.snapshot()
        )
    }

    private func runSubAgents(_ assignments: [VoiceSubAgentAssignment]) async throws -> [VoiceSubAgentResult] {
        try await withThrowingTaskGroup(of: VoiceSubAgentResult.self) { group in
            for assignment in assignments {
                guard let subAgent = subAgents[assignment.agentName] else {
                    throw VoiceAgentError.unknownSubAgent(assignment.agentName)
                }

                group.addTask {
                    try await subAgent.run(assignment)
                }
            }

            var results: [VoiceSubAgentResult] = []
            for try await result in group {
                results.append(result)
            }
            return results
        }
    }

    private static func buildSupervisorPrompt(
        input: String,
        results: [VoiceSubAgentResult]
    ) -> String {
        let rendered = results.map { result in
            """
            [\(result.agentName)]
            Task: \(result.task)
            Output:
            \(result.output)
            """
        }.joined(separator: "\n\n")

        return """
        User request:
        \(input)

        Subagent results:
        \(rendered)

        Produce the final answer for the user. Resolve disagreements, keep it concise, and preserve useful details.
        """
    }
}
