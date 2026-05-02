import Foundation
#if canImport(Security)
import Security
#endif

/// 运行时配置集中点：环境变量、Keychain、默认 base URL / 模型。
public enum VoiceAgentRuntimeConfig {
    public static let apiKeyEnvironmentVariables = [
        "AHAKEY_OPENAI_API_KEY",
        "OPENAI_API_KEY",
    ]
    public static let baseURLEnvironmentVariable = "AHAKEY_OPENAI_BASE_URL"
    public static let modelEnvironmentVariable = "AHAKEY_OPENAI_MODEL"
    public static let keychainService = "com.ahakey.voiceagent"
    public static let keychainAPIKeyAccount = "openai-compatible-api-key"
    public static let keychainBaseURLAccount = "openai-compatible-base-url"
    public static let keychainModelAccount = "openai-compatible-model"

    // MARK: - Feishu / Lark

    public static let feishuContactsEnvironmentVariable = "AHAKEY_FEISHU_CONTACTS_JSON"

    public static let defaultOpenAIBaseURL = URL(string: "https://api.openai-next.com/v1")!
    public static let defaultModel = "claude-opus-4-7"

    public static var openAIBaseURL: URL {
        openAIBaseURL(environment: ProcessInfo.processInfo.environment)
    }

    public static var openAIAPIKey: String? {
        openAIAPIKey(environment: ProcessInfo.processInfo.environment)
    }

    public static var openAIModel: String {
        openAIModel(environment: ProcessInfo.processInfo.environment)
    }

    public static func openAIBaseURL(
        environment: [String: String] = ProcessInfo.processInfo.environment
    ) -> URL {
        // 优先环境变量
        if let rawValue = nonEmpty(environment[baseURLEnvironmentVariable]),
           let url = URL(string: rawValue),
           url.scheme != nil, url.host != nil {
            return url
        }
        // 其次 Keychain
        if let rawValue = VoiceAgentKeychain.openAIAPIKey(service: keychainService, account: keychainBaseURLAccount),
           let url = URL(string: rawValue),
           url.scheme != nil, url.host != nil {
            return url
        }
        return defaultOpenAIBaseURL
    }

    public static func openAIAPIKey(
        environment: [String: String] = ProcessInfo.processInfo.environment,
        includeKeychain: Bool = true
    ) -> String? {
        for variable in apiKeyEnvironmentVariables {
            if let apiKey = nonEmpty(environment[variable]) {
                return apiKey
            }
        }

        guard includeKeychain else { return nil }
        return VoiceAgentKeychain.openAIAPIKey(
            service: keychainService,
            account: keychainAPIKeyAccount
        )
    }

    public static func openAIModel(
        environment: [String: String] = ProcessInfo.processInfo.environment
    ) -> String {
        if let envModel = nonEmpty(environment[modelEnvironmentVariable]) {
            return envModel
        }
        if let keychainModel = VoiceAgentKeychain.openAIAPIKey(service: keychainService, account: keychainModelAccount),
           !keychainModel.isEmpty {
            return keychainModel
        }
        return defaultModel
    }

    // MARK: - Feishu accessors

    public static var feishuContactsJSON: String? {
        feishuContactsJSON(environment: ProcessInfo.processInfo.environment)
    }

    public static func feishuContactsJSON(
        environment: [String: String] = ProcessInfo.processInfo.environment
    ) -> String? {
        nonEmpty(environment[feishuContactsEnvironmentVariable])
    }

    private static func nonEmpty(_ value: String?) -> String? {
        guard let value else { return nil }
        let trimmed = value.trimmingCharacters(in: .whitespacesAndNewlines)
        return trimmed.isEmpty ? nil : trimmed
    }
}

/// macOS Keychain 读写封装。
public enum VoiceAgentKeychain {
    public static func openAIAPIKey(service: String, account: String) -> String? {
        #if canImport(Security)
        let query: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: service,
            kSecAttrAccount as String: account,
            kSecReturnData as String: true,
            kSecMatchLimit as String: kSecMatchLimitOne,
        ]

        var item: CFTypeRef?
        let status = SecItemCopyMatching(query as CFDictionary, &item)
        guard status == errSecSuccess, let data = item as? Data else {
            return nil
        }
        guard let value = String(data: data, encoding: .utf8) else {
            return nil
        }
        let trimmed = value.trimmingCharacters(in: .whitespacesAndNewlines)
        return trimmed.isEmpty ? nil : trimmed
        #else
        return nil
        #endif
    }

    @discardableResult
    public static func saveToKeychain(service: String, account: String, value: String) -> Bool {
        #if canImport(Security)
        guard let data = value.data(using: .utf8) else { return false }

        // 先尝试更新
        let query: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: service,
            kSecAttrAccount as String: account,
        ]
        let attributes: [String: Any] = [
            kSecValueData as String: data,
        ]
        let updateStatus = SecItemUpdate(query as CFDictionary, attributes as CFDictionary)
        if updateStatus == errSecSuccess {
            return true
        }

        // 不存在则添加
        var addQuery = query
        addQuery[kSecValueData as String] = data
        let addStatus = SecItemAdd(addQuery as CFDictionary, nil)
        return addStatus == errSecSuccess
        #else
        return false
        #endif
    }
}
