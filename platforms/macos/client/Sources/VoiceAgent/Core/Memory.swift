import Foundation

public struct VoiceAgentMemorySnapshot: Equatable, Sendable {
    public var facts: [String: String]
    public var notes: [String]

    public var rendered: String {
        var lines: [String] = []
        if !facts.isEmpty {
            lines.append("Facts:")
            for key in facts.keys.sorted() {
                lines.append("- \(key): \(facts[key] ?? "")")
            }
        }
        if !notes.isEmpty {
            lines.append("Notes:")
            for note in notes {
                lines.append("- \(note)")
            }
        }
        return lines.isEmpty ? "No memory yet." : lines.joined(separator: "\n")
    }
}

public actor VoiceAgentMemory {
    private var facts: [String: String]
    private var notes: [String]

    public init(facts: [String: String] = [:], notes: [String] = []) {
        self.facts = facts
        self.notes = notes
    }

    public func setFact(_ key: String, value: String) {
        facts[key] = value
    }

    public func fact(_ key: String) -> String? {
        facts[key]
    }

    public func remember(_ note: String) {
        notes.append(note)
    }

    public func snapshot() -> VoiceAgentMemorySnapshot {
        VoiceAgentMemorySnapshot(facts: facts, notes: notes)
    }

    public func reset() {
        facts = [:]
        notes = []
    }
}
