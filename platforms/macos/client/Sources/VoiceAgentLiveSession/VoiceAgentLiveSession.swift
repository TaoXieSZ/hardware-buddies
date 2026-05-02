import Foundation
import VoiceAgent

@main
enum VoiceAgentLiveSessionRunner {
    static func main() async {
        let runner = VoiceAgentRunner.configuredOpenAI(
            systemPrompt: """
            你是 AhaKey Mode 2 的智能语音助手，负责总管所有的事项。
            你可以直接回答简单问题。
            接下来是你核心的任务：## 当你认为需要拆分任务时候，你需要综合情况委派不同的子 agent去完成任务。##
            原则是：
            1. 只有当子任务彼此独立，且并行处理明显优于你自己串行完成时，才使用 subagent。
            2. 你需要统筹全局决定，这需要你成为一个富有洞见和规划能力的CEO。如何最优化完成任务是你要考虑的东西。
            
            对于简单查询、总结、格式整理、依赖前序结果的任务，请直接完成。
            """,
            options: VoiceAgentOptions(temperature: 0.3, maxTokens: 2048),
            onEvent: { event in
                print("  \(event)")
            }
        )

        let turns = Array(CommandLine.arguments.dropFirst())
        if !turns.isEmpty {
            for text in turns {
                await send(text, to: runner)
            }
            return
        }

        await runInteractiveSession(runner: runner)
    }

    private static func runInteractiveSession(runner: VoiceAgentRunner) async {
        print("VoiceAgent started (with subagent tool). Type a message, .exit to quit.")
        print("")

        while true {
            print("> ", terminator: "")
            guard let line = readLine(strippingNewline: true) else {
                print("")
                break
            }

            let text = line.trimmingCharacters(in: .whitespacesAndNewlines)
            if text.isEmpty { continue }

            switch text {
            case ".exit", ":q", "quit":
                print("Session ended.")
                return
            case ".reset":
                await runner.reset()
                print("Session reset.")
            default:
                await send(text, to: runner)
            }
        }
    }

    private static func send(_ text: String, to runner: VoiceAgentRunner) async {
        do {
            let response = try await runner.send(text)
            print("\n\(response)\n")
        } catch {
            fputs("Error: \(error.localizedDescription)\n", stderr)
        }
    }
}
