import Foundation

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
    public var toolCalls: [OpenAIToolCall]?

    public init(
        role: VoiceAgentRole,
        content: String,
        name: String? = nil,
        toolCallID: String? = nil,
        toolCalls: [OpenAIToolCall]? = nil
    ) {
        self.role = role
        self.content = content
        self.name = name
        self.toolCallID = toolCallID
        self.toolCalls = toolCalls
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
        case toolCalls = "tool_calls"
    }

    // OpenAI returns content=null when tool_calls is present, decode defensively.
    public init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        role = try container.decode(VoiceAgentRole.self, forKey: .role)
        content = try container.decodeIfPresent(String.self, forKey: .content) ?? ""
        name = try container.decodeIfPresent(String.self, forKey: .name)
        toolCallID = try container.decodeIfPresent(String.self, forKey: .toolCallID)
        toolCalls = try container.decodeIfPresent([OpenAIToolCall].self, forKey: .toolCalls)
    }

    public func encode(to encoder: Encoder) throws {
        var container = encoder.container(keyedBy: CodingKeys.self)
        try container.encode(role, forKey: .role)
        try container.encode(content, forKey: .content)
        try container.encodeIfPresent(name, forKey: .name)
        try container.encodeIfPresent(toolCallID, forKey: .toolCallID)
        try container.encodeIfPresent(toolCalls, forKey: .toolCalls)
    }
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

public struct VoiceAgentOptions: Equatable, Sendable {
    public var temperature: Double?
    public var maxTokens: Int?

    public init(temperature: Double? = nil, maxTokens: Int? = nil) {
        self.temperature = temperature
        self.maxTokens = maxTokens
    }
}
