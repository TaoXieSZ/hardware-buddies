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

    // MARK: - Feishu / Lark

    public static let feishuAppIDEnvironmentVariable = "AHAKEY_FEISHU_APP_ID"
    public static let feishuAppSecretEnvironmentVariable = "AHAKEY_FEISHU_APP_SECRET"
    public static let feishuContactsEnvironmentVariable = "AHAKEY_FEISHU_CONTACTS_JSON"
    public static let keychainFeishuAppIDAccount = "feishu-app-id"
    public static let keychainFeishuAppSecretAccount = "feishu-app-secret"
    public static let feishuBaseURL = URL(string: "https://open.feishu.cn/open-apis")!

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
        guard
            let rawValue = nonEmpty(environment[baseURLEnvironmentVariable]),
            let url = URL(string: rawValue),
            url.scheme != nil,
            url.host != nil
        else {
            return defaultOpenAIBaseURL
        }
        return url
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
        nonEmpty(environment[modelEnvironmentVariable]) ?? defaultModel
    }

    // MARK: - Feishu accessors

    public static var feishuAppID: String? {
        feishuAppID(environment: ProcessInfo.processInfo.environment)
    }

    public static var feishuAppSecret: String? {
        feishuAppSecret(environment: ProcessInfo.processInfo.environment)
    }

    public static var feishuContactsJSON: String? {
        feishuContactsJSON(environment: ProcessInfo.processInfo.environment)
    }

    public static func feishuAppID(
        environment: [String: String] = ProcessInfo.processInfo.environment,
        includeKeychain: Bool = true
    ) -> String? {
        if let value = nonEmpty(environment[feishuAppIDEnvironmentVariable]) {
            return value
        }
        guard includeKeychain else { return nil }
        return VoiceAgentKeychain.openAIAPIKey(
            service: keychainService,
            account: keychainFeishuAppIDAccount
        )
    }

    public static func feishuAppSecret(
        environment: [String: String] = ProcessInfo.processInfo.environment,
        includeKeychain: Bool = true
    ) -> String? {
        if let value = nonEmpty(environment[feishuAppSecretEnvironmentVariable]) {
            return value
        }
        guard includeKeychain else { return nil }
        return VoiceAgentKeychain.openAIAPIKey(
            service: keychainService,
            account: keychainFeishuAppSecretAccount
        )
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

/// 从 macOS Keychain 读取 OpenAI 兼容 API key 的轻封装。
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
}
