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

public enum VoiceAgentError: Error, LocalizedError, Equatable {
    case missingAPIKey
    case invalidEndpoint(URL)
    case emptyResponse
    case badStatusCode(Int, String)

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
