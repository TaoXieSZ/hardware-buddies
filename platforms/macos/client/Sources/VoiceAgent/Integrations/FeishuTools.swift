import Foundation

// MARK: - Send Message Tool

/// 飞书发消息工具：支持按预指定联系人名字或直接用 ID 发送。
/// 通过 lark-cli 以用户身份发送，无需 App 自身存储飞书凭证。
public struct FeishuSendMessageTool: VoiceAgentTool {
    public let name = "feishu_send_message"
    public let description = """
        Send a message via Feishu/Lark. \
        You can specify a contact by name, alias, email, mobile, or direct ID (use the 'contact' field), \
        or directly provide 'receive_id' + 'receive_id_type'. \
        The message content should be plain text.
        """

    public let parameters: JSONValue = .object([
        "type": .string("object"),
        "properties": .object([
            "contact": .object([
                "type": .string("string"),
                "description": .string("Contact name/alias/email/mobile/direct ID. Resolves local aliases."),
            ]),
            "receive_id": .object([
                "type": .string("string"),
                "description": .string("Direct Feishu ID (open_id, user_id, chat_id, or email). Use when 'contact' is not available."),
            ]),
            "receive_id_type": .object([
                "type": .string("string"),
                "enum": .array([.string("open_id"), .string("user_id"), .string("chat_id"), .string("email")]),
                "description": .string("Type of receive_id. Defaults to open_id."),
            ]),
            "message": .object([
                "type": .string("string"),
                "description": .string("The text message to send."),
            ]),
        ]),
        "required": .array([.string("message")]),
    ])

    private let contactBook: FeishuContactBook

    public init(contacts: [FeishuContact] = []) {
        self.contactBook = FeishuContactBook(contacts: contacts)
    }

    public func call(input: String, context: VoiceAgentToolContext) async throws -> String {
        let args = try parseJSON(input)

        let message = args["message"] ?? ""
        guard !message.isEmpty else {
            return "Error: message cannot be empty."
        }

        let receiveID: String
        let receiveIDType: String

        if let contactName = args["contact"] {
            let resolved = resolveContact(contactName)
            switch resolved {
            case let .single(contact):
                receiveID = contact.id
                receiveIDType = contact.idType.rawValue
            case let .multiple(matches):
                return "Error: multiple contacts matched '\(contactName)'. Specify one of:\n\(renderContacts(matches))"
            case .none:
                return "Error: contact '\(contactName)' not found in local contacts. Provide receive_id directly."
            }
        } else if let directID = args["receive_id"] {
            receiveID = directID
            receiveIDType = args["receive_id_type"] ?? "open_id"
        } else {
            return "Error: provide either 'contact' name or 'receive_id'."
        }

        // 用 lark-cli 以用户身份发消息
        let result: String
        if receiveIDType == "chat_id" {
            result = try await runLarkCLI(args: [
                "im", "+messages-send", "--as", "user",
                "--chat-id", receiveID,
                "--text", message,
            ])
        } else if receiveIDType == "open_id" || receiveIDType == "user_id" {
            result = try await runLarkCLI(args: [
                "im", "+messages-send", "--as", "user",
                "--user-id", receiveID,
                "--text", message,
            ])
        } else {
            // email: 转成 lark-cli 格式
            result = try await runLarkCLI(args: [
                "im", "+messages-send", "--as", "user",
                "--email", receiveID,
                "--text", message,
            ])
        }

        if result.contains("\"ok\": true") || result.contains("\"ok\":true") || result.contains("message_id") {
            return "Message sent successfully (as user). \(result)"
        } else {
            return "Error sending message: \(result)"
        }
    }

    private func resolveContact(_ query: String) -> ContactResolution {
        let localMatches = contactBook.matches(query)
        if localMatches.count == 1 {
            return .single(localMatches[0])
        }
        if localMatches.count > 1 {
            return .multiple(localMatches)
        }
        return .none
    }
}

// MARK: - Lookup Contact Tool

/// 查询联系人：先查本地预配置，再用 lark-cli 搜群聊和通讯录。
public struct FeishuLookupContactTool: VoiceAgentTool {
    public let name = "feishu_lookup_contact"
    public let description = """
        Look up a Feishu contact or group chat by name. \
        Checks local pre-configured contacts first, then searches Feishu via lark-cli \
        (group chats and user directory). Returns the ID and type for sending messages.
        """

    public let parameters: JSONValue = .object([
        "type": .string("object"),
        "properties": .object([
            "name": .object([
                "type": .string("string"),
                "description": .string("Contact or group chat name to look up."),
            ]),
        ]),
        "required": .array([.string("name")]),
    ])

    private let contactBook: FeishuContactBook

    public init(contacts: [FeishuContact]) {
        self.contactBook = FeishuContactBook(contacts: contacts)
    }

    public func call(input: String, context: VoiceAgentToolContext) async throws -> String {
        let args = try parseJSON(input)

        guard let name = args["name"], !name.isEmpty else {
            if contactBook.contacts.isEmpty {
                return "No local contacts configured. Provide a name to search Feishu."
            }
            return "Available local contacts:\n\(renderContacts(contactBook.contacts))"
        }

        // 1. 先查本地
        let localMatches = contactBook.matches(name)
        if localMatches.count == 1 {
            return "Found local contact: \(renderContact(localMatches[0]))"
        }
        if localMatches.count > 1 {
            return "Multiple local contacts matched '\(name)':\n\(renderContacts(localMatches))"
        }

        // 2. 用 lark-cli 搜群聊
        let chatResult = try await runLarkCLI(args: [
            "im", "+chat-search", "--as", "user",
            "--query", name, "--page-size", "5", "--format", "json",
        ])
        let chatMatches = parseChatSearchResult(chatResult)
        if !chatMatches.isEmpty {
            return "Found group chat(s):\n\(renderContacts(chatMatches))"
        }

        // 3. 用 lark-cli 搜联系人
        let userResult = try await runLarkCLI(args: [
            "contact", "+search-user", "--as", "user",
            "--query", name, "--page-size", "5", "--format", "json",
        ])
        let userMatches = parseUserSearchResult(userResult)
        if !userMatches.isEmpty {
            return "Found user(s):\n\(renderContacts(userMatches))"
        }

        return "No results for '\(name)'. Try a different keyword, or use a direct Feishu ID."
    }

    private func parseChatSearchResult(_ json: String) -> [FeishuContact] {
        guard let data = json.data(using: .utf8),
              let obj = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
              let ok = obj["ok"] as? Bool, ok,
              let resultData = obj["data"] as? [String: Any],
              let chats = resultData["chats"] as? [[String: Any]]
        else { return [] }

        return chats.compactMap { chat in
            guard let chatID = chat["chat_id"] as? String,
                  let chatName = chat["name"] as? String
            else { return nil }
            return FeishuContact(name: chatName, id: chatID, idType: .chatID)
        }
    }

    private func parseUserSearchResult(_ json: String) -> [FeishuContact] {
        guard let data = json.data(using: .utf8),
              let obj = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
              let ok = obj["ok"] as? Bool, ok,
              let resultData = obj["data"] as? [String: Any],
              let users = resultData["users"] as? [[String: Any]]
        else { return [] }

        return users.compactMap { user in
            guard let openID = user["open_id"] as? String else { return nil }
            let name = (user["name"] as? String) ?? (user["en_name"] as? String) ?? openID
            return FeishuContact(name: name, id: openID, idType: .openID)
        }
    }
}

// MARK: - JSON helpers

private enum ContactResolution {
    case none
    case single(FeishuContact)
    case multiple([FeishuContact])
}

private struct FeishuContactBook {
    let contacts: [FeishuContact]

    init(contacts: [FeishuContact]) {
        self.contacts = contacts
    }

    func matches(_ query: String) -> [FeishuContact] {
        let normalizedQuery = normalize(query)
        guard !normalizedQuery.isEmpty else { return [] }

        let exact = contacts.filter { contact in
            lookupKeys(for: contact).contains(normalizedQuery)
        }
        if !exact.isEmpty {
            return exact
        }

        return contacts.filter { contact in
            lookupKeys(for: contact).contains { $0.contains(normalizedQuery) || normalizedQuery.contains($0) }
        }
    }

    private func lookupKeys(for contact: FeishuContact) -> [String] {
        ([contact.name, contact.id] + contact.aliases).map(normalize)
    }

    private func normalize(_ value: String) -> String {
        value.trimmingCharacters(in: .whitespacesAndNewlines).lowercased()
    }
}

private func renderContacts(_ contacts: [FeishuContact]) -> String {
    contacts
        .sorted { $0.name < $1.name }
        .map(renderContact)
        .joined(separator: "\n")
}

private func renderContact(_ contact: FeishuContact) -> String {
    let aliasText = contact.aliases.isEmpty ? "" : " aliases: \(contact.aliases.joined(separator: ", "))"
    return "\(contact.name) — \(contact.idType.rawValue): \(contact.id)\(aliasText)"
}

private func parseJSON(_ input: String) throws -> [String: String] {
    guard let data = input.data(using: .utf8),
          let obj = try? JSONSerialization.jsonObject(with: data) as? [String: Any]
    else {
        return [:]
    }
    var result: [String: String] = [:]
    for (key, value) in obj {
        if let s = value as? String {
            result[key] = s
        } else if let n = value as? NSNumber {
            result[key] = n.stringValue
        }
    }
    return result
}

/// 查找 lark-cli 可执行文件路径。优先 app bundle 内置，其次系统 PATH。
func findLarkCLIPath() -> String? {
    var paths: [String] = []

    // 优先 app bundle 内置的 lark-cli
    if let bundlePath = Bundle.main.executableURL?
        .deletingLastPathComponent()
        .appendingPathComponent("lark-cli").path,
       FileManager.default.fileExists(atPath: bundlePath) {
        paths.insert(bundlePath, at: 0)
    }

    paths += [
        "/usr/local/bin/lark-cli",
        "/opt/homebrew/bin/lark-cli",
        ProcessInfo.processInfo.environment["HOME"].map { "\($0)/.npm-global/bin/lark-cli" },
    ].compactMap { $0 }

    return paths.first { FileManager.default.fileExists(atPath: $0) }
}

/// 调用 lark-cli 命令行工具，返回 stdout 输出。
private func runLarkCLI(args: [String]) async throws -> String {
    try await withCheckedThrowingContinuation { continuation in
        let process = Process()
        let pipe = Pipe()

        guard let execPath = findLarkCLIPath() else {
            continuation.resume(throwing: FeishuError.apiFailed("lark-cli not found. Please ensure it is bundled with the app or installed on your system."))
            return
        }

        process.executableURL = URL(fileURLWithPath: execPath)
        process.arguments = args
        process.standardOutput = pipe
        process.standardError = pipe

        do {
            try process.run()
        } catch {
            continuation.resume(throwing: FeishuError.apiFailed("Failed to launch lark-cli: \(error.localizedDescription)"))
            return
        }

        process.waitUntilExit()
        let data = pipe.fileHandleForReading.readDataToEndOfFile()
        let output = String(data: data, encoding: .utf8) ?? ""
        continuation.resume(returning: output)
    }
}

