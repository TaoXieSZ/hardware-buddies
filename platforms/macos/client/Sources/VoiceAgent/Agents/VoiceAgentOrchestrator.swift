import Foundation

public struct VoiceAgentOrchestrationResult: Equatable, Sendable {
    public var input: String
    public var finalOutput: String
    public var subAgentResults: [VoiceSubAgentResult]
    public var supervisorSession: VoiceAgentSessionSnapshot
}

public actor VoiceAgentOrchestrator {
    private let supervisor: VoiceAgent
    private var subAgents: [String: VoiceSubAgent]
    private var handles: [UUID: VoiceSubAgentHandle] = [:]

    public init(supervisor: VoiceAgent, subAgents: [VoiceSubAgent]) {
        self.supervisor = supervisor
        self.subAgents = Dictionary(uniqueKeysWithValues: subAgents.map { ($0.name, $0) })
    }

    public func addSubAgent(_ subAgent: VoiceSubAgent) {
        subAgents[subAgent.name] = subAgent
    }

    public func availableSubAgents() -> [String] {
        subAgents.keys.sorted()
    }

    /// `tokio::spawn` 等价：立即派生一个并发 `Task` 跑这次 assignment，
    /// 返回 handle 给 supervisor 持有。Task 在派生瞬间已开始执行，handle
    /// 会被 orchestrator 缓存到 `handles[runID]` 直到 orchestrator 释放，
    /// 以便 supervisor 在整个生命周期内随时读取该 run 的会话/记忆。
    @discardableResult
    public func spawn(_ assignment: VoiceSubAgentAssignment) throws -> VoiceSubAgentHandle {
        guard let subAgent = subAgents[assignment.agentName] else {
            throw VoiceAgentError.unknownSubAgent(assignment.agentName)
        }
        let runID = UUID()
        let runTask = Task<VoiceSubAgentResult, Error> {
            try await subAgent.run(assignment)
        }
        let handle = VoiceSubAgentHandle(
            runID: runID,
            agentName: assignment.agentName,
            task: assignment.task,
            spawnedAt: Date(),
            subAgent: subAgent,
            runTask: runTask
        )
        handles[runID] = handle
        return handle
    }

    public func spawnAll(_ assignments: [VoiceSubAgentAssignment]) throws -> [VoiceSubAgentHandle] {
        try assignments.map { try spawn($0) }
    }

    public func handle(runID: UUID) -> VoiceSubAgentHandle? {
        handles[runID]
    }

    public func handles(forName name: String) -> [VoiceSubAgentHandle] {
        handles.values.filter { $0.agentName == name }
    }

    public func liveHandles() -> [VoiceSubAgentHandle] {
        Array(handles.values)
    }

    public func liveSnapshots() async -> [VoiceSubAgentLiveSnapshot] {
        var out: [VoiceSubAgentLiveSnapshot] = []
        out.reserveCapacity(handles.count)
        for handle in handles.values {
            out.append(await handle.liveSnapshot())
        }
        return out
    }

    public func subAgentSessionSnapshot(name: String) async throws -> VoiceAgentSessionSnapshot {
        guard let subAgent = subAgents[name] else {
            throw VoiceAgentError.unknownSubAgent(name)
        }
        return await subAgent.sessionSnapshot()
    }

    public func subAgentMemorySnapshot(name: String) async throws -> VoiceAgentMemorySnapshot {
        guard let subAgent = subAgents[name] else {
            throw VoiceAgentError.unknownSubAgent(name)
        }
        return await subAgent.memorySnapshot()
    }

    public func cancelAll() async {
        for handle in handles.values {
            await handle.cancel()
        }
    }

    public func run(_ input: String) async throws -> VoiceAgentOrchestrationResult {
        let assignments = subAgents.keys.sorted().map {
            VoiceSubAgentAssignment(agentName: $0, task: input)
        }
        return try await run(input, assignments: assignments)
    }

    public func run(
        _ input: String,
        assignments: [VoiceSubAgentAssignment]
    ) async throws -> VoiceAgentOrchestrationResult {
        let spawned = try spawnAll(assignments)
        let results: [VoiceSubAgentResult]
        do {
            results = try await joinAll(spawned)
        } catch {
            for handle in spawned {
                await handle.cancel()
            }
            throw error
        }
        let sorted = results.sorted { $0.agentName < $1.agentName }

        let finalPrompt = Self.buildSupervisorPrompt(input: input, results: sorted)
        let finalOutput = try await supervisor.send(finalPrompt)
        return VoiceAgentOrchestrationResult(
            input: input,
            finalOutput: finalOutput,
            subAgentResults: sorted,
            supervisorSession: await supervisor.snapshot()
        )
    }

    /// Sequentially `await` each handle. Tasks run concurrently regardless,
    /// so wall time is `max(durations)` — equivalent to tokio's `join_all`.
    private func joinAll(_ spawned: [VoiceSubAgentHandle]) async throws -> [VoiceSubAgentResult] {
        var results: [VoiceSubAgentResult] = []
        results.reserveCapacity(spawned.count)
        for handle in spawned {
            results.append(try await handle.value())
        }
        return results
    }

    private static func buildSupervisorPrompt(
        input: String,
        results: [VoiceSubAgentResult]
    ) -> String {
        let rendered = results.map { result in
            """
            [\(result.agentName)]
            Task: \(result.task)
            Output:
            \(result.output)
            """
        }.joined(separator: "\n\n")

        return """
        User request:
        \(input)

        Subagent results:
        \(rendered)

        Produce the final answer for the user. Resolve disagreements, keep it concise, and preserve useful details.
        """
    }
}
