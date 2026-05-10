import Foundation

struct AppConfig {
    let apiKey: String?
    let language: String?
}

/// Reads OPENAI_API_KEY env var first; if missing, falls back to
/// ~/.config/buddy-helper/config.toml [stt] section.
func loadConfig() -> AppConfig {
    if let envKey = ProcessInfo.processInfo.environment["OPENAI_API_KEY"],
       !envKey.isEmpty {
        return AppConfig(apiKey: envKey, language: nil)
    }
    return parseConfigToml()
}

// ---------------------------------------------------------------------------
// Tiny TOML parser — supports only [section] headers and key = "value" lines
// ---------------------------------------------------------------------------

private func parseConfigToml() -> AppConfig {
    let home = NSHomeDirectory()
    let path = "\(home)/.config/buddy-helper/config.toml"
    guard let contents = try? String(contentsOfFile: path, encoding: .utf8) else {
        return AppConfig(apiKey: nil, language: nil)
    }

    var currentSection = ""
    var apiKey: String?
    var language: String?

    for rawLine in contents.components(separatedBy: "\n") {
        let line = rawLine.trimmingCharacters(in: .whitespaces)
        if line.isEmpty || line.hasPrefix("#") { continue }

        // Section header: [stt]
        if line.hasPrefix("[") && line.hasSuffix("]") {
            currentSection = String(line.dropFirst().dropLast())
            continue
        }

        // key = "value"
        guard let eqRange = line.range(of: "=") else { continue }
        let key = line[line.startIndex..<eqRange.lowerBound]
            .trimmingCharacters(in: .whitespaces)
        var value = line[eqRange.upperBound...]
            .trimmingCharacters(in: .whitespaces)
        // Strip surrounding quotes
        if value.hasPrefix("\"") && value.hasSuffix("\"") && value.count >= 2 {
            value = String(value.dropFirst().dropLast())
        }

        if currentSection == "stt" {
            if key == "api_key" { apiKey = value }
            if key == "language" { language = value }
        }
    }

    return AppConfig(apiKey: apiKey, language: language.flatMap { $0.isEmpty ? nil : $0 })
}
