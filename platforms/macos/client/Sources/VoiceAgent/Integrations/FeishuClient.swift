import Foundation

// MARK: - Contact model

/// 预指定的飞书联系人，用于 subagent 按名字查找收信人。
public struct FeishuContact: Sendable, Equatable, Codable {
    public enum IDType: String, Sendable, Equatable, Codable {
        case openID = "open_id"
        case userID = "user_id"
        case chatID = "chat_id"
        case email = "email"
    }

    public let name: String
    public let id: String
    public let idType: IDType
    public let aliases: [String]

    public init(name: String, id: String, idType: IDType, aliases: [String] = []) {
        self.name = name
        self.id = id
        self.idType = idType
        self.aliases = aliases
    }

    enum CodingKeys: String, CodingKey {
        case name, id, aliases
        case idType = "id_type"
    }
}

public extension FeishuContact {
    /// 解析顺序:Application Support 下的本地文件 → AHAKEY_FEISHU_CONTACTS_JSON 环境变量。
    /// JSON 示例：[{"name":"智能助手","id":"xxx@example.com","id_type":"email","aliases":["助手"]}]
    static func configuredContacts(
        environment: [String: String] = ProcessInfo.processInfo.environment
    ) -> [FeishuContact] {
        let stored = FeishuContactStore.load()
        if !stored.isEmpty {
            return stored
        }
        guard
            let json = VoiceAgentRuntimeConfig.feishuContactsJSON(environment: environment),
            let data = json.data(using: .utf8)
        else {
            return []
        }
        return (try? JSONDecoder().decode([FeishuContact].self, from: data)) ?? []
    }
}

/// 把飞书联系人持久化到 ~/Library/Application Support/AhakeyConfig/feishu-contacts.json。
public enum FeishuContactStore {
    public static let directoryName = "AhakeyConfig"
    public static let fileName = "feishu-contacts.json"

    public static var fileURL: URL? {
        guard let support = try? FileManager.default.url(
            for: .applicationSupportDirectory,
            in: .userDomainMask,
            appropriateFor: nil,
            create: true
        ) else { return nil }
        return support
            .appendingPathComponent(directoryName, isDirectory: true)
            .appendingPathComponent(fileName)
    }

    public static func load() -> [FeishuContact] {
        guard
            let url = fileURL,
            FileManager.default.fileExists(atPath: url.path),
            let data = try? Data(contentsOf: url),
            let contacts = try? JSONDecoder().decode([FeishuContact].self, from: data)
        else { return [] }
        return contacts
    }

    @discardableResult
    public static func save(_ contacts: [FeishuContact]) -> Bool {
        guard let url = fileURL else { return false }
        do {
            try FileManager.default.createDirectory(
                at: url.deletingLastPathComponent(),
                withIntermediateDirectories: true
            )
            let encoder = JSONEncoder()
            encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
            let data = try encoder.encode(contacts)
            try data.write(to: url, options: .atomic)
            return true
        } catch {
            return false
        }
    }
}

// MARK: - Error

public enum FeishuError: Error, LocalizedError {
    case apiFailed(String)

    public var errorDescription: String? {
        switch self {
        case let .apiFailed(msg): "Feishu error: \(msg)"
        }
    }
}
