import Foundation

/// 抽象的 LLM 客户端接口；可注入 mock 用于测试或替换为其他兼容协议的提供方。
public protocol VoiceAgentLLMClient: Sendable {
    func complete(_ request: OpenAIChatCompletionRequest) async throws -> VoiceAgentMessage
}

/// OpenAI 协议兼容的 chat/completions 客户端实现。
public final class OpenAICompatibleChatClient: VoiceAgentLLMClient, @unchecked Sendable {
    private let endpoint: URL
    private let apiKeyProvider: @Sendable () -> String?
    private let additionalHeaders: [String: String]
    private let session: URLSession
    private let encoder: JSONEncoder
    private let decoder: JSONDecoder

    public init(
        baseURL: URL = URL(string: "https://api.openai-next.com/v1")!,
        apiKeyProvider: @escaping @Sendable () -> String?,
        additionalHeaders: [String: String] = [:],
        session: URLSession? = nil
    ) {
        self.endpoint = baseURL.appendingPathComponent("chat/completions")
        self.apiKeyProvider = apiKeyProvider
        self.additionalHeaders = additionalHeaders
        if let session {
            self.session = session
        } else {
            let config = URLSessionConfiguration.default
            config.timeoutIntervalForRequest = 300
            config.timeoutIntervalForResource = 600
            self.session = URLSession(configuration: config)
        }
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
        if let apiError = completion.error {
            let msg = apiError.message ?? "Unknown API error"
            throw VoiceAgentError.badStatusCode(httpResponse.statusCode, msg)
        }
        guard let message = completion.choices.first?.message else {
            throw VoiceAgentError.emptyResponse
        }
        return message
    }
}

public extension OpenAICompatibleChatClient {
    static func configuredOpenAI() -> OpenAICompatibleChatClient {
        OpenAICompatibleChatClient(
            baseURL: VoiceAgentRuntimeConfig.openAIBaseURL,
            apiKeyProvider: { VoiceAgentRuntimeConfig.openAIAPIKey }
        )
    }
}
