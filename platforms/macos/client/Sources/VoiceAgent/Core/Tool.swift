import Foundation

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

/// Agent 系统统一工具协议。
/// `VoiceAgentRunner` 和 `VoiceSubAgent` 共用同一套工具定义：
/// runner 自动把 `openAIDefinition()` 发给 LLM 做 function calling，
/// 调用时构建 `VoiceAgentToolContext` 传入 `call(input:context:)`。
public protocol VoiceAgentTool: Sendable {
    var name: String { get }
    var description: String { get }
    /// JSON Schema describing the tool's input parameters (OpenAI function calling format).
    var parameters: JSONValue { get }

    func call(input: String, context: VoiceAgentToolContext) async throws -> String
}

// MARK: - Defaults & Convenience

public extension VoiceAgentTool {
    /// Default: tool accepts no parameters.
    var parameters: JSONValue {
        .object(["type": .string("object"), "properties": .object([:])])
    }

    /// Convert this tool to an `OpenAIToolDefinition` for LLM function calling.
    func openAIDefinition() -> OpenAIToolDefinition {
        OpenAIToolDefinition(
            function: .init(
                name: name,
                description: description,
                parameters: parameters
            )
        )
    }
}

// MARK: - Closure adapter

/// Wrap a bare closure as a `VoiceAgentTool`.
public struct ClosureVoiceAgentTool: VoiceAgentTool {
    public let name: String
    public let description: String
    public let parameters: JSONValue
    private let handler: @Sendable (String, VoiceAgentToolContext) async throws -> String

    public init(
        name: String,
        description: String,
        parameters: JSONValue = .object(["type": .string("object"), "properties": .object([:])]),
        handler: @escaping @Sendable (String, VoiceAgentToolContext) async throws -> String
    ) {
        self.name = name
        self.description = description
        self.parameters = parameters
        self.handler = handler
    }

    /// Convenience: wrap a context-free closure.
    public init(
        name: String,
        description: String,
        parameters: JSONValue = .object(["type": .string("object"), "properties": .object([:])]),
        simpleHandler: @escaping @Sendable (String) async throws -> String
    ) {
        self.name = name
        self.description = description
        self.parameters = parameters
        self.handler = { input, _ in try await simpleHandler(input) }
    }

    public func call(input: String, context: VoiceAgentToolContext) async throws -> String {
        try await handler(input, context)
    }
}
