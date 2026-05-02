import Foundation

/// Actor-based semaphore that limits concurrent async work.
/// 仅在 `VoiceAgentRunner` 内部使用，限制 LLM 调用并发以避免提供方限速。
actor ConcurrencyLimiter {
    private let limit: Int
    private var running = 0
    private var waiters: [CheckedContinuation<Void, Never>] = []

    init(limit: Int) { self.limit = limit }

    func acquire() async {
        if running < limit {
            running += 1
            return
        }
        await withCheckedContinuation { cont in
            waiters.append(cont)
        }
    }

    func release() {
        if let next = waiters.first {
            waiters.removeFirst()
            next.resume()
        } else {
            running -= 1
        }
    }
}
