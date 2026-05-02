import Foundation

public enum VoiceAgentRunKind: String, Codable, Equatable, Sendable {
    case root
    case subagent
}

public enum VoiceAgentRunStatus: String, Codable, Equatable, Sendable {
    case running
    case completed
    case failed
}

public enum VoiceAgentToolCallStatus: String, Codable, Equatable, Sendable {
    case running
    case completed
    case failed
    case skipped
}

public struct VoiceAgentToolCallSnapshot: Codable, Equatable, Sendable, Identifiable {
    public var id: String { callID }

    public var callID: String
    public var name: String
    public var arguments: String
    public var status: VoiceAgentToolCallStatus
    public var output: String?
    public var error: String?
    public var startedAt: Date
    public var completedAt: Date?
}

public struct VoiceAgentRunSnapshot: Codable, Equatable, Sendable, Identifiable {
    public var id: UUID { runID }

    public var runID: UUID
    public var rootRunID: UUID
    public var parentRunID: UUID?
    public var kind: VoiceAgentRunKind
    public var depth: Int
    public var title: String
    public var status: VoiceAgentRunStatus
    public var messages: [VoiceAgentMessage]
    public var toolCalls: [VoiceAgentToolCallSnapshot]
    public var childRunIDs: [UUID]
    public var output: String?
    public var error: String?
    public var startedAt: Date
    public var completedAt: Date?
}

public enum VoiceAgentRunEvent: Sendable {
    case runStarted(VoiceAgentRunSnapshot)
    case messageAppended(runID: UUID, message: VoiceAgentMessage)
    case toolStarted(runID: UUID, toolCall: VoiceAgentToolCallSnapshot)
    case toolFinished(runID: UUID, toolCall: VoiceAgentToolCallSnapshot)
    case runCompleted(runID: UUID, output: String)
    case runFailed(runID: UUID, error: String)
    case notice(runID: UUID?, message: String)

    public var displayText: String {
        switch self {
        case let .runStarted(snapshot):
            switch snapshot.kind {
            case .root:
                "[run] \(snapshot.title)"
            case .subagent:
                "[subagent depth=\(snapshot.depth)] \(snapshot.title)"
            }
        case let .messageAppended(_, message):
            "[message] \(message.role.rawValue)"
        case let .toolStarted(_, toolCall):
            "[tool] \(toolCall.name)"
        case let .toolFinished(_, toolCall):
            switch toolCall.status {
            case .completed:
                "[tool] \(toolCall.name) completed"
            case .failed:
                "[error] tool \(toolCall.name) failed: \(toolCall.error ?? "unknown error")"
            case .skipped:
                "[tool] \(toolCall.name) skipped"
            case .running:
                "[tool] \(toolCall.name)"
            }
        case let .runCompleted(runID, _):
            "[run completed] \(runID.uuidString)"
        case let .runFailed(runID, error):
            "[run failed] \(runID.uuidString): \(error)"
        case let .notice(_, message):
            message
        }
    }
}

public actor VoiceAgentRunRegistry {
    private var runs: [UUID: VoiceAgentRunSnapshot] = [:]
    private var orderedRunIDs: [UUID] = []

    public init() {}

    @discardableResult
    public func startRun(
        runID: UUID = UUID(),
        kind: VoiceAgentRunKind,
        title: String,
        parentRunID: UUID?,
        rootRunID: UUID?,
        depth: Int,
        messages: [VoiceAgentMessage]
    ) -> VoiceAgentRunSnapshot {
        let resolvedRootRunID = rootRunID ?? runID
        let snapshot = VoiceAgentRunSnapshot(
            runID: runID,
            rootRunID: resolvedRootRunID,
            parentRunID: parentRunID,
            kind: kind,
            depth: depth,
            title: title,
            status: .running,
            messages: messages,
            toolCalls: [],
            childRunIDs: [],
            output: nil,
            error: nil,
            startedAt: Date(),
            completedAt: nil
        )
        runs[runID] = snapshot
        orderedRunIDs.append(runID)

        if let parentRunID, var parent = runs[parentRunID] {
            parent.childRunIDs.append(runID)
            runs[parentRunID] = parent
        }

        return snapshot
    }

    public func appendMessage(_ message: VoiceAgentMessage, to runID: UUID) {
        guard var run = runs[runID] else { return }
        run.messages.append(message)
        runs[runID] = run
    }

    @discardableResult
    public func startToolCall(
        callID: String,
        name: String,
        arguments: String,
        in runID: UUID
    ) -> VoiceAgentToolCallSnapshot {
        let snapshot = VoiceAgentToolCallSnapshot(
            callID: callID,
            name: name,
            arguments: arguments,
            status: .running,
            output: nil,
            error: nil,
            startedAt: Date(),
            completedAt: nil
        )
        guard var run = runs[runID] else { return snapshot }
        run.toolCalls.append(snapshot)
        runs[runID] = run
        return snapshot
    }

    @discardableResult
    public func finishToolCall(
        callID: String,
        in runID: UUID,
        status: VoiceAgentToolCallStatus,
        output: String? = nil,
        error: String? = nil
    ) -> VoiceAgentToolCallSnapshot? {
        guard var run = runs[runID],
              let index = run.toolCalls.firstIndex(where: { $0.callID == callID })
        else { return nil }

        run.toolCalls[index].status = status
        run.toolCalls[index].output = output
        run.toolCalls[index].error = error
        run.toolCalls[index].completedAt = Date()
        let toolCall = run.toolCalls[index]
        runs[runID] = run
        return toolCall
    }

    public func completeRun(_ runID: UUID, output: String) {
        guard var run = runs[runID] else { return }
        run.status = .completed
        run.output = output
        run.completedAt = Date()
        runs[runID] = run
    }

    public func failRun(_ runID: UUID, error: String) {
        guard var run = runs[runID] else { return }
        run.status = .failed
        run.error = error
        run.completedAt = Date()
        runs[runID] = run
    }

    public func snapshot(runID: UUID) -> VoiceAgentRunSnapshot? {
        runs[runID]
    }

    public func snapshots() -> [VoiceAgentRunSnapshot] {
        orderedRunIDs.compactMap { runs[$0] }
    }

    public func reset() {
        runs = [:]
        orderedRunIDs = []
    }
}
