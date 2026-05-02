import Foundation

// MARK: - Function calling

public struct OpenAIFunctionCall: Codable, Sendable, Equatable {
    public var name: String
    public var arguments: String

    public init(name: String, arguments: String) {
        self.name = name
        self.arguments = arguments
    }
}

public struct OpenAIToolCall: Codable, Sendable, Equatable {
    public var id: String
    public var type: String
    public var function: OpenAIFunctionCall

    public init(id: String, type: String = "function", function: OpenAIFunctionCall) {
        self.id = id
        self.type = type
        self.function = function
    }
}

public struct OpenAIToolDefinition: Codable, Sendable {
    public var type: String
    public var function: FunctionSpec

    public struct FunctionSpec: Codable, Sendable {
        public var name: String
        public var description: String
        public var parameters: JSONValue

        public init(name: String, description: String, parameters: JSONValue) {
            self.name = name
            self.description = description
            self.parameters = parameters
        }
    }

    public init(type: String = "function", function: FunctionSpec) {
        self.type = type
        self.function = function
    }
}

// MARK: - Chat completion request / response

public struct OpenAIChatCompletionRequest: Codable, Sendable {
    public var model: String
    public var messages: [VoiceAgentMessage]
    public var temperature: Double?
    public var maxTokens: Int?
    public var stream: Bool?
    public var tools: [OpenAIToolDefinition]?

    public init(
        model: String,
        messages: [VoiceAgentMessage],
        temperature: Double? = nil,
        maxTokens: Int? = nil,
        stream: Bool? = nil,
        tools: [OpenAIToolDefinition]? = nil
    ) {
        self.model = model
        self.messages = messages
        self.temperature = temperature
        self.maxTokens = maxTokens
        self.stream = stream
        self.tools = tools
    }

    private enum CodingKeys: String, CodingKey {
        case model
        case messages
        case temperature
        case maxTokens = "max_tokens"
        case stream
        case tools
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

    /// API error envelope – some providers return `{"error": {...}}` with HTTP 200.
    public var error: APIError?

    public struct APIError: Codable, Sendable {
        public var message: String?
        public var type: String?
        public var code: String?
    }

    private enum CodingKeys: String, CodingKey {
        case id, object, created, model, choices, usage, error
    }

    public init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        id = try container.decodeIfPresent(String.self, forKey: .id)
        object = try container.decodeIfPresent(String.self, forKey: .object)
        created = try container.decodeIfPresent(Int.self, forKey: .created)
        model = try container.decodeIfPresent(String.self, forKey: .model)
        choices = try container.decodeIfPresent([Choice].self, forKey: .choices) ?? []
        usage = try container.decodeIfPresent(Usage.self, forKey: .usage)
        error = try container.decodeIfPresent(APIError.self, forKey: .error)
    }
}
