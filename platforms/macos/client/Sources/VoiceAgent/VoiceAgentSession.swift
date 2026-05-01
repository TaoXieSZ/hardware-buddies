#if VOICE_AGENT_LIVE_SESSION
import Foundation

@main
private enum VoiceAgentLiveSessionRunner {
    static func main() async {
        let agent = VoiceAgent.hardcodedOpenAI(
            systemPrompt: "你是 AhaKey Mode 2 的语音主 Agent。你要保持多轮上下文，回答简洁、直接。",
            options: VoiceAgentOptions(temperature: 0.2, maxTokens: 1024)
        )

        let turns = Array(CommandLine.arguments.dropFirst())
        if !turns.isEmpty {
            await runScriptedSession(agent: agent, turns: turns)
            return
        }

        await runInteractiveSession(agent: agent)
    }

    private static func runScriptedSession(agent: VoiceAgent, turns: [String]) async {
        print("VoiceAgent live session started. turns=\(turns.count)")
        for text in turns {
            await send(text, to: agent)
        }
        await printSessionSummary(agent)
    }

    private static func runInteractiveSession(agent: VoiceAgent) async {
        print("VoiceAgent live session started.")
        print("Type a message and press Return. Commands: .exit, .reset, .history")

        while true {
            print("> ", terminator: "")
            guard let line = readLine(strippingNewline: true) else {
                print("")
                break
            }

            let text = line.trimmingCharacters(in: .whitespacesAndNewlines)
            if text.isEmpty {
                continue
            }

            switch text {
            case ".exit", ":q", "quit":
                await printSessionSummary(agent)
                return
            case ".reset":
                await agent.reset()
                print("Session reset.")
            case ".history":
                await printHistory(agent)
            default:
                await send(text, to: agent)
            }
        }
    }

    private static func send(_ text: String, to agent: VoiceAgent) async {
        do {
            let turn = try await agent.sendTurn(text)
            print("")
            print("User[\(turn.index)]: \(turn.userMessage.content)")
            print("Assistant[\(turn.index)]: \(turn.assistantMessage.content)")
            print("")
        } catch {
            fputs("VoiceAgent request failed: \(error.localizedDescription)\n", stderr)
        }
    }

    private static func printHistory(_ agent: VoiceAgent) async {
        let snapshot = await agent.snapshot()
        print("Session \(snapshot.sessionID.uuidString) messages=\(snapshot.messages.count) turns=\(snapshot.turnCount)")
        for (index, message) in snapshot.messages.enumerated() {
            print("\(index + 1). \(message.role.rawValue): \(message.content)")
        }
    }

    private static func printSessionSummary(_ agent: VoiceAgent) async {
        let snapshot = await agent.snapshot()
        print("Session ended. id=\(snapshot.sessionID.uuidString) messages=\(snapshot.messages.count) turns=\(snapshot.turnCount)")
    }
}
#endif
