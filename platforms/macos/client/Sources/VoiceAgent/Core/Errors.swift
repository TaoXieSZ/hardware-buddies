import Foundation

public enum VoiceAgentError: Error, LocalizedError, Equatable {
    case missingAPIKey
    case invalidEndpoint(URL)
    case emptyResponse
    case badStatusCode(Int, String)
    case unknownSubAgent(String)
    case unknownTool(agentName: String, toolName: String)
    case runAlreadyInProgress

    public var errorDescription: String? {
        switch self {
        case .missingAPIKey:
            "Missing OpenAI-compatible API key."
        case let .invalidEndpoint(url):
            "Invalid OpenAI-compatible endpoint: \(url.absoluteString)"
        case .emptyResponse:
            "The model returned no assistant message."
        case let .badStatusCode(code, body):
            "OpenAI-compatible endpoint returned HTTP \(code): \(body)"
        case let .unknownSubAgent(name):
            "Unknown subagent: \(name)"
        case let .unknownTool(agentName, toolName):
            "Unknown tool '\(toolName)' for subagent '\(agentName)'."
        case .runAlreadyInProgress:
            "A voice agent run is already in progress."
        }
    }
}
