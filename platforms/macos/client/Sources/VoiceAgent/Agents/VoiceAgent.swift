import Foundation

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
            // 失败时回滚刚追加的 user 消息，避免后续重试携带孤立的 user 帧。
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
    static func configuredOpenAI(
        model: String = VoiceAgentRuntimeConfig.openAIModel,
        systemPrompt: String? = nil,
        options: VoiceAgentOptions = VoiceAgentOptions()
    ) -> VoiceAgent {
        VoiceAgent(
            model: model,
            systemPrompt: systemPrompt,
            options: options,
            client: OpenAICompatibleChatClient.configuredOpenAI()
        )
    }
}
