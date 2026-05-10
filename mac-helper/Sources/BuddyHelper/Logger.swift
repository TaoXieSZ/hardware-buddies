import Foundation
import os

// Minimal logger backed by os.Logger. Writes to stderr; launchd routes that
// to ~/Library/Logs/buddy-helper/stderr.log via the plist StandardErrorPath.
// A separate stdout.log is available for any prints directed there.

enum LogLevel {
    case info, warn, error
}

private let osLog = os.Logger(subsystem: "com.buddyhelper", category: "main")

func log(_ message: String, level: LogLevel = .info) {
    let prefix: String
    switch level {
    case .info:  prefix = "[INFO]"
    case .warn:  prefix = "[WARN]"
    case .error: prefix = "[ERROR]"
    }
    let line = "\(prefix) \(message)"
    switch level {
    case .info:  osLog.info("\(message, privacy: .public)")
    case .warn:  osLog.warning("\(message, privacy: .public)")
    case .error: osLog.error("\(message, privacy: .public)")
    }
    // Also mirror to stderr so launchd captures it in stderr.log
    var stderr = FileHandle.standardError
    let data = (line + "\n").data(using: .utf8) ?? Data()
    stderr.write(data)
}
