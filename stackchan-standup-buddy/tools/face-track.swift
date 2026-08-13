// Mac helper for stackchan-standup-buddy.
// The device is authoritative: MODE WORK/FREE enables Vision tracking;
// MODE OFF stops the camera. CLOCK only synchronizes RTC. A detached,
// ephemeral `codex exec` keeps one short philosophy cached for head pats.

import AVFoundation
import Vision
import Foundation
import Darwin

setbuf(stdout, nil)
func log(_ text: String) { FileHandle.standardError.write(Data((text + "\n").utf8)) }

let args = CommandLine.arguments
guard args.count >= 2 else {
    log("usage: face-track <serial-port> [camera-name-substring]")
    exit(2)
}
let portPath = args[1]
let nameFilter = args.count >= 3 ? args[2].lowercased() : nil
let fd = open(portPath, O_RDWR | O_NOCTTY | O_NONBLOCK)
guard fd >= 0 else { log("cannot open \(portPath): \(String(cString: strerror(errno)))"); exit(1) }

let writeLock = NSLock()
let send: @Sendable (String) -> Void = { text in
    writeLock.lock(); defer { writeLock.unlock() }
    text.withCString { _ = write(fd, $0, strlen($0)) }
}

func sendClock() {
    let c = Calendar.current.dateComponents([.year, .month, .day, .hour, .minute], from: Date())
    let day = (c.year ?? 0) * 10000 + (c.month ?? 0) * 100 + (c.day ?? 0)
    send("CLOCK \(day) \((c.hour ?? 0) * 60 + (c.minute ?? 0))\n")
}

final class WisdomCache {
    private let lock = NSLock()
    private var cached: String?
    private var generating = false
    private var waiting = false

    func request() {
        lock.lock()
        if let text = cached {
            cached = nil
            lock.unlock()
            send("WISDOM \(text)\n")
            refill()
        } else {
            waiting = true
            let start = !generating
            lock.unlock()
            if start { refill() }
        }
    }

    func refill() {
        lock.lock()
        if generating || cached != nil { lock.unlock(); return }
        generating = true
        lock.unlock()

        DispatchQueue.global(qos: .utility).async {
            let output = FileManager.default.temporaryDirectory
                .appendingPathComponent("stackchan-wisdom-\(UUID().uuidString).txt")
            defer { try? FileManager.default.removeItem(at: output) }
            let task = Process()
            task.executableURL = URL(fileURLWithPath: "/usr/bin/env")
            task.arguments = ["codex", "exec", "--ephemeral", "--sandbox", "read-only",
                              "--skip-git-repo-check", "--ignore-user-config", "--ignore-rules",
                              "-C", "/tmp", "-o", output.path,
                              "只输出一句中文人生小哲理：暖、幽默、不说教，不超过28个汉字，不加引号或解释。"]
            task.standardOutput = FileHandle.nullDevice
            task.standardError = FileHandle.nullDevice
            try? task.run()
            // This runs on a utility queue and never blocks a head-pat response.
            // Let the one cache fill finish naturally instead of killing Codex's
            // child process and risking an orphan when startup is slow/offline.
            if task.isRunning { task.waitUntilExit() }
            var text = (try? String(contentsOf: output, encoding: .utf8)) ?? ""
            text = text.replacingOccurrences(of: "\n", with: " ").trimmingCharacters(in: .whitespacesAndNewlines)
            if text.count > 34 { text = String(text.prefix(34)) }

            self.lock.lock()
            self.generating = false
            let deliver = self.waiting && !text.isEmpty
            self.waiting = false
            if !deliver && !text.isEmpty { self.cached = text }
            self.lock.unlock()
            if deliver { send("WISDOM \(text)\n"); self.refill() }
        }
    }
}

let wisdom = WisdomCache()
wisdom.refill()

let discovery = AVCaptureDevice.DiscoverySession(
    deviceTypes: [.builtInWideAngleCamera, .continuityCamera, .external],
    mediaType: .video, position: .unspecified)
var cameras = discovery.devices
if let filter = nameFilter { cameras = cameras.filter { $0.localizedName.lowercased().contains(filter) } }
guard let camera = cameras.first ?? AVCaptureDevice.default(for: .video) else {
    log("no camera found. available: \(discovery.devices.map { $0.localizedName })")
    exit(1)
}

final class Tracker: NSObject, AVCaptureVideoDataOutputSampleBufferDelegate {
    private var lastProcess = Date.distantPast
    private var lastReport = Date.distantPast
    private let request = VNDetectFaceRectanglesRequest()
    func captureOutput(_ output: AVCaptureOutput, didOutput sampleBuffer: CMSampleBuffer,
                       from connection: AVCaptureConnection) {
        let now = Date()
        guard now.timeIntervalSince(lastProcess) >= 0.1 else { return }
        lastProcess = now
        guard let buffer = CMSampleBufferGetImageBuffer(sampleBuffer) else { return }
        try? VNImageRequestHandler(cvPixelBuffer: buffer, orientation: .up).perform([request])
        if let face = (request.results ?? []).max(by: {
            $0.boundingBox.width * $0.boundingBox.height < $1.boundingBox.width * $1.boundingBox.height
        }) {
            let cx = Int((face.boundingBox.midX * 2 - 1) * 1000)
            let cy = Int((face.boundingBox.midY * 2 - 1) * 1000)
            send("TRACK \(cx) \(cy) \(Int(face.confidence * 1000))\n")
            if now.timeIntervalSince(lastReport) >= 1 { lastReport = now; log("face cx=\(cx) cy=\(cy)") }
        } else {
            send("TRACK LOST\n")
            if now.timeIntervalSince(lastReport) >= 1 { lastReport = now; log("no face") }
        }
    }
}

let session = AVCaptureSession()
session.sessionPreset = .vga640x480
do {
    let input = try AVCaptureDeviceInput(device: camera)
    guard session.canAddInput(input) else { throw NSError(domain: "face-track", code: 1) }
    session.addInput(input)
} catch { log("cannot open camera \(camera.localizedName)"); exit(1) }
let output = AVCaptureVideoDataOutput()
output.alwaysDiscardsLateVideoFrames = true
let tracker = Tracker()
output.setSampleBufferDelegate(tracker, queue: DispatchQueue(label: "face-track.frames"))
guard session.canAddOutput(output) else { exit(1) }
session.addOutput(output)

let stateLock = NSLock()
var deviceMonitoring = false
func setDeviceMonitoring(_ value: Bool) {
    stateLock.lock(); deviceMonitoring = value; stateLock.unlock()
}

let serialSource = DispatchSource.makeReadSource(fileDescriptor: fd, queue: DispatchQueue.global())
var serialBuffer = Data()
serialSource.setEventHandler {
    var bytes = [UInt8](repeating: 0, count: 512)
    let count = read(fd, &bytes, bytes.count)
    guard count > 0 else { return }
    serialBuffer.append(contentsOf: bytes.prefix(count))
    while let newline = serialBuffer.firstIndex(of: 10) {
        let line = String(data: serialBuffer.prefix(upTo: newline), encoding: .utf8) ?? ""
        serialBuffer.removeSubrange(...newline)
        if line == "MODE WORK" || line == "MODE FREE" { setDeviceMonitoring(true) }
        else if line == "MODE OFF" { setDeviceMonitoring(false) }
        else if line == "WISDOM_REQUEST" { wisdom.request() }
    }
}
serialSource.resume()

sendClock()
var lastClock = Date()
Timer.scheduledTimer(withTimeInterval: 1, repeats: true) { _ in
    stateLock.lock(); let wanted = deviceMonitoring; stateLock.unlock()
    if wanted && !session.isRunning { session.startRunning(); log("device mode: camera on") }
    else if !wanted && session.isRunning { session.stopRunning(); log("device mode: camera off") }
    if Date().timeIntervalSince(lastClock) >= 55 { lastClock = Date(); sendClock() }
}

print("face-track: \(camera.localizedName) -> \(portPath); waiting for device MODE")
RunLoop.main.run()
