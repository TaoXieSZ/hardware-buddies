#if DEBUG
import Foundation

private struct MockVoiceAgentClient: VoiceAgentLLMClient {
    var handler: @Sendable (OpenAIChatCompletionRequest) async throws -> VoiceAgentMessage

    func complete(_ request: OpenAIChatCompletionRequest) async throws -> VoiceAgentMessage {
        try await handler(request)
    }
}

private struct MockVoiceAgentTool: VoiceAgentTool {
    var name: String
    var description: String
    var handler: @Sendable (String, VoiceAgentToolContext) async throws -> String

    func call(input: String, context: VoiceAgentToolContext) async throws -> String {
        try await handler(input, context)
    }
}

private actor RequestRecorder {
    private var requests: [OpenAIChatCompletionRequest] = []

    func record(_ request: OpenAIChatCompletionRequest) -> Int {
        requests.append(request)
        return requests.count
    }

    func all() -> [OpenAIChatCompletionRequest] {
        requests
    }
}

private actor SubAgentEventRecorder {
    private var events: [VoiceSubAgentEvent] = []

    func record(_ event: VoiceSubAgentEvent) {
        events.append(event)
    }

    func all() -> [VoiceSubAgentEvent] {
        events
    }
}

enum VoiceAgentSelfTests {
    static func runAll() async throws {
        try testOpenAIRequestEncoding()
        try testHardcodedConfigDefaults()
        try await testConversationHistory()
        try await testMultiTurnSessionCarriesContext()
        try await testSubAgentMemoryAndTools()
        try await testOrchestratorRunsSubAgentsAndSynthesizes()
        try await testUnknownToolError()
        try await testResetKeepsSystemPrompt()
        try await testEmptyResponseError()
    }

    private static func testOpenAIRequestEncoding() throws {
        let request = OpenAIChatCompletionRequest(
            model: "gpt-test",
            messages: [
                .system("You are concise."),
                .user("hello"),
                .tool("lookup result", toolCallID: "call_123"),
            ],
            temperature: 0.2,
            maxTokens: 128,
            stream: false
        )

        let data = try JSONEncoder().encode(request)
        let object = try requireDictionary(from: data)

        try expect(object["model"] as? String == "gpt-test", "model should encode")
        try expect(object["temperature"] as? Double == 0.2, "temperature should encode")
        try expect(object["max_tokens"] as? Int == 128, "max_tokens should encode")
        try expect(object["stream"] as? Bool == false, "stream should encode")

        let messages = try requireArray(object["messages"], "messages should encode")
        let toolMessage = try requireDictionary(messages[2], "tool message should encode")
        try expect(toolMessage["tool_call_id"] as? String == "call_123", "tool_call_id should encode")
    }

    private static func testHardcodedConfigDefaults() throws {
        let baseURL = VoiceAgentHardcodedConfig.openAIBaseURL
        try expect(
            baseURL.scheme == "https"
                && baseURL.host?.isEmpty == false
                && !baseURL.path.isEmpty,
            "hardcoded OpenAI-compatible base URL should be valid, got scheme=\(baseURL.scheme ?? "nil") host=\(baseURL.host ?? "nil") path=\(baseURL.path) absolute=\(baseURL.absoluteString)"
        )
        try expect(
            !VoiceAgentHardcodedConfig.defaultModel.isEmpty,
            "hardcoded default model should not be empty"
        )
        try expect(
            !VoiceAgentHardcodedConfig.openAIAPIKey.isEmpty,
            "hardcoded API key placeholder should not be empty"
        )
    }

    private static func testConversationHistory() async throws {
        let client = MockVoiceAgentClient { request in
            try expect(request.messages.map(\.role) == [.system, .user], "request should include system + user")
            try expect(request.messages.last?.content == "请总结今天的工作", "user text should be forwarded")
            return .assistant("已总结")
        }

        let agent = VoiceAgent(
            model: "gpt-test",
            systemPrompt: "你是语音助手。",
            client: client
        )

        let output = try await agent.send("请总结今天的工作")
        try expect(output == "已总结", "assistant output should return")

        let history = await agent.history()
        try expect(history.map(\.role) == [.system, .user, .assistant], "history should append assistant")
        try expect(history.last?.content == "已总结", "history should store assistant content")
    }

    private static func testMultiTurnSessionCarriesContext() async throws {
        let recorder = RequestRecorder()
        let client = MockVoiceAgentClient { request in
            let count = await recorder.record(request)
            return .assistant("assistant turn \(count)")
        }

        let agent = VoiceAgent(
            model: "gpt-test",
            systemPrompt: "你会记住上下文。",
            client: client
        )

        let firstTurn = try await agent.sendTurn("我的名字是阿哈。")
        try expect(firstTurn.index == 1, "first turn index should be 1")

        let secondTurn = try await agent.sendTurn("我叫什么？")
        try expect(secondTurn.index == 2, "second turn index should be 2")

        let requests = await recorder.all()
        try expect(requests.count == 2, "client should receive two requests")
        try expect(
            requests[1].messages.map(\.role) == [.system, .user, .assistant, .user],
            "second request should include full prior context"
        )
        try expect(
            requests[1].messages.map(\.content) == [
                "你会记住上下文。",
                "我的名字是阿哈。",
                "assistant turn 1",
                "我叫什么？",
            ],
            "second request should preserve message order"
        )

        let snapshot = await agent.snapshot()
        try expect(snapshot.turnCount == 2, "snapshot should count user turns")
        try expect(snapshot.messages.count == 5, "snapshot should contain system and two completed turns")
    }

    private static func testSubAgentMemoryAndTools() async throws {
        let recorder = RequestRecorder()
        let events = SubAgentEventRecorder()
        let client = MockVoiceAgentClient { request in
            let count = await recorder.record(request)
            return .assistant("subagent answer \(count)")
        }
        let lookup = MockVoiceAgentTool(
            name: "lookup",
            description: "Return lookup data."
        ) { input, context in
            try expect(context.agentName == "researcher", "tool context should include agent name")
            return "lookup(\(input))"
        }

        let subAgent = VoiceSubAgent(
            name: "researcher",
            purpose: "Find facts.",
            model: "gpt-test",
            systemPrompt: "You are a researcher.",
            client: client,
            tools: [lookup],
            eventHandler: { event in
                await events.record(event)
            }
        )

        let first = try await subAgent.run(
            VoiceSubAgentAssignment(
                agentName: "researcher",
                task: "查一下蓝莓",
                toolInvocations: [
                    VoiceAgentToolInvocation(name: "lookup", input: "blueberry"),
                ]
            )
        )

        try expect(first.output == "subagent answer 1", "subagent should return model output")
        try expect(first.toolResults == [
            VoiceAgentToolResult(name: "lookup", input: "blueberry", output: "lookup(blueberry)"),
        ], "subagent should expose tool results")
        try expect(first.memory.rendered.contains("Tool lookup returned: lookup(blueberry)"), "memory should retain tool result")

        let firstRunEvents = await events.all()
        try expect(
            firstRunEvents.map(\.phase) == [.started, .toolStarted, .toolFinished, .completed],
            "subagent should explicitly emit start/tool/completed lifecycle events"
        )
        try expect(
            Set(firstRunEvents.map(\.runID)) == [first.runID],
            "all first-run events should use the result runID"
        )
        try expect(
            firstRunEvents.first?.agentName == "researcher"
                && firstRunEvents.first?.task == "查一下蓝莓",
            "started event should identify subagent and task"
        )

        _ = try await subAgent.run(
            VoiceSubAgentAssignment(agentName: "researcher", task: "复述刚才查到的内容")
        )

        let requests = await recorder.all()
        try expect(requests.count == 2, "subagent should make two model requests")
        try expect(
            requests[1].messages.last?.content.contains("Tool lookup returned: lookup(blueberry)") == true,
            "second subagent request should include persisted memory"
        )
        try expect(
            requests[1].messages.last?.content.contains("Task: 查一下蓝莓") == true,
            "second subagent request should include previous task memory"
        )
    }

    private static func testOrchestratorRunsSubAgentsAndSynthesizes() async throws {
        let client = MockVoiceAgentClient { request in
            let prompt = request.messages.last?.content ?? ""
            if prompt.contains("Subagent: planner") {
                return .assistant("planner result")
            }
            if prompt.contains("Subagent: critic") {
                return .assistant("critic result")
            }
            if prompt.contains("Subagent results:") {
                try expect(prompt.contains("planner result"), "supervisor should see planner result")
                try expect(prompt.contains("critic result"), "supervisor should see critic result")
                return .assistant("final synthesis")
            }
            return .assistant("unexpected")
        }

        let supervisor = VoiceAgent(
            model: "gpt-test",
            systemPrompt: "You supervise subagents.",
            client: client
        )
        let planner = VoiceSubAgent(
            name: "planner",
            purpose: "Create a plan.",
            model: "gpt-test",
            systemPrompt: "You plan.",
            client: client
        )
        let critic = VoiceSubAgent(
            name: "critic",
            purpose: "Find risks.",
            model: "gpt-test",
            systemPrompt: "You critique.",
            client: client
        )
        let orchestrator = VoiceAgentOrchestrator(
            supervisor: supervisor,
            subAgents: [planner, critic]
        )

        let result = try await orchestrator.run("设计 Mode 2 的工作流")
        try expect(result.finalOutput == "final synthesis", "orchestrator should return supervisor output")
        try expect(result.subAgentResults.map(\.agentName) == ["critic", "planner"], "subagent results should be sorted")
        try expect(result.subAgentResults.map(\.output).sorted() == ["critic result", "planner result"], "orchestrator should include both subagent outputs")
        try expect(result.supervisorSession.turnCount == 1, "supervisor should keep orchestration memory")
    }

    private static func testUnknownToolError() async throws {
        let client = MockVoiceAgentClient { _ in .assistant("unused") }
        let subAgent = VoiceSubAgent(
            name: "worker",
            purpose: "Work.",
            model: "gpt-test",
            systemPrompt: "You work.",
            client: client
        )

        do {
            _ = try await subAgent.run(
                VoiceSubAgentAssignment(
                    agentName: "worker",
                    task: "call missing tool",
                    toolInvocations: [
                        VoiceAgentToolInvocation(name: "missing", input: "x"),
                    ]
                )
            )
            throw SelfTestFailure("unknown tool should throw")
        } catch VoiceAgentError.unknownTool(let agentName, let toolName) {
            try expect(agentName == "worker", "unknown tool error should include agent name")
            try expect(toolName == "missing", "unknown tool error should include tool name")
        }
    }

    private static func testResetKeepsSystemPrompt() async throws {
        let client = MockVoiceAgentClient { _ in .assistant("ok") }
        let agent = VoiceAgent(
            model: "gpt-test",
            systemPrompt: "system",
            client: client
        )

        try await agent.send("hello")
        await agent.reset()

        let history = await agent.history()
        try expect(history == [.system("system")], "reset should keep system prompt by default")
    }

    private static func testEmptyResponseError() async throws {
        let data = Data("""
        {
          "id": "chatcmpl-test",
          "object": "chat.completion",
          "created": 0,
          "model": "gpt-test",
          "choices": []
        }
        """.utf8)

        let response = try JSONDecoder().decode(OpenAIChatCompletionResponse.self, from: data)
        try expect(response.choices.isEmpty, "empty choices response should decode")
    }

    private static func requireDictionary(from data: Data) throws -> [String: Any] {
        let object = try JSONSerialization.jsonObject(with: data)
        return try requireDictionary(object, "root object should be dictionary")
    }

    private static func requireDictionary(_ value: Any, _ message: String) throws -> [String: Any] {
        guard let dictionary = value as? [String: Any] else {
            throw SelfTestFailure(message)
        }
        return dictionary
    }

    private static func requireArray(_ value: Any?, _ message: String) throws -> [Any] {
        guard let array = value as? [Any] else {
            throw SelfTestFailure(message)
        }
        return array
    }

    private static func expect(_ condition: @autoclosure () -> Bool, _ message: String) throws {
        if !condition() {
            throw SelfTestFailure(message)
        }
    }
}

private struct SelfTestFailure: Error, CustomStringConvertible {
    let description: String

    init(_ description: String) {
        self.description = description
    }
}

#if VOICE_AGENT_RUN_TESTS
@main
private enum VoiceAgentTestRunner {
    static func main() async {
        do {
            try await VoiceAgentSelfTests.runAll()
            print("VoiceAgent self-tests passed.")
        } catch {
            fputs("VoiceAgent self-tests failed: \(error)\n", stderr)
            Foundation.exit(1)
        }
    }
}
#endif
#endif
