// face-track — Mac-side face detection for stackchan-standup-buddy.
//
// Uses the webcam (built-in / USB / Continuity Camera) + Vision face
// detection, and streams the face centre to the StackChan over USB serial:
//
//     TRACK <cx_pm> <cy_pm> <conf_pm>\n   cx/cy: -1000..1000 per-mille of frame
//     TRACK LOST\n                      no face in this frame
// (cy positive = face in the upper half of the frame)
//
// The firmware turns those into yaw servo targets (deadzone + low-pass + P).
//
// Build:  swiftc -O tools/face-track.swift -o tools/face-track
// Run:    ./tools/face-track /dev/cu.usbmodem2101 [camera-name-substring]
//
// First run triggers a macOS camera-permission prompt for your terminal app.

import AVFoundation
import Vision
import Foundation

setbuf(stdout, nil)   // unbuffered: we're a long-running daemon on a pipe
func log(_ s: String) { FileHandle.standardError.write(Data((s + "\n").utf8)) }

let args = CommandLine.arguments
guard args.count >= 2 else {
    FileHandle.standardError.write(Data("usage: face-track <serial-port> [camera-name-substring]\n".utf8))
    exit(2)
}
let portPath = args[1]
let nameFilter = args.count >= 3 ? args[2].lowercased() : nil

let fd = open(portPath, O_WRONLY | O_NOCTTY | O_NONBLOCK)
guard fd >= 0 else {
    FileHandle.standardError.write(Data("cannot open \(portPath): \(String(cString: strerror(errno)))\n".utf8))
    exit(1)
}
func send(_ s: String) {
    s.withCString { _ = write(fd, $0, strlen($0)) }
}

// Wall clock for the firmware's work-hours gating: "TIME <minutes since
// midnight>" on startup and once a minute (the firmware interpolates between).
func sendTime() {
    let c = Calendar.current.dateComponents([.hour, .minute], from: Date())
    send("TIME \((c.hour ?? 0) * 60 + (c.minute ?? 0))\n")
}

// ---- camera selection --------------------------------------------------------
let discovery = AVCaptureDevice.DiscoverySession(
    deviceTypes: [.builtInWideAngleCamera, .continuityCamera, .external],
    mediaType: .video, position: .unspecified)
var cameras = discovery.devices
if let f = nameFilter { cameras = cameras.filter { $0.localizedName.lowercased().contains(f) } }
guard let cam = cameras.first ?? AVCaptureDevice.default(for: .video) else {
    FileHandle.standardError.write(Data("no camera found. available: \(discovery.devices.map { $0.localizedName })\n".utf8))
    exit(1)
}

// ---- face detection → serial --------------------------------------------------
final class Tracker: NSObject, AVCaptureVideoDataOutputSampleBufferDelegate {
    private var lastProcess = Date.distantPast
    private var lastReport  = Date.distantPast
    private let request = VNDetectFaceRectanglesRequest()

    func captureOutput(_ output: AVCaptureOutput,
                       didOutput sampleBuffer: CMSampleBuffer,
                       from connection: AVCaptureConnection) {
        let now = Date()
        guard now.timeIntervalSince(lastProcess) >= 0.1 else { return }  // ~10 Hz
        lastProcess = now
        guard let pb = CMSampleBufferGetImageBuffer(sampleBuffer) else { return }
        let handler = VNImageRequestHandler(cvPixelBuffer: pb, orientation: .up, options: [:])
        try? handler.perform([request])
        let faces = request.results ?? []
        if let face = faces.max(by: { $0.boundingBox.width * $0.boundingBox.height
                                    < $1.boundingBox.width * $1.boundingBox.height }) {
            let cxPm   = Int((face.boundingBox.midX * 2 - 1) * 1000)
            let cyPm   = Int((face.boundingBox.midY * 2 - 1) * 1000)
            let confPm = Int(face.confidence * 1000)
            send("TRACK \(cxPm) \(cyPm) \(confPm)\n")
            if now.timeIntervalSince(lastReport) >= 1.0 {
                lastReport = now
                log("face cx=\(cxPm) cy=\(cyPm) conf=\(confPm)")
            }
        } else {
            send("TRACK LOST\n")
            if now.timeIntervalSince(lastReport) >= 1.0 {
                lastReport = now
                log("no face")
            }
        }
    }
}

let session = AVCaptureSession()
session.sessionPreset = .vga640x480
do {
    let input = try AVCaptureDeviceInput(device: cam)
    guard session.canAddInput(input) else { throw NSError(domain: "face-track", code: 1) }
    session.addInput(input)
} catch {
    FileHandle.standardError.write(Data("cannot open camera \(cam.localizedName)\n".utf8))
    exit(1)
}
let output = AVCaptureVideoDataOutput()
output.alwaysDiscardsLateVideoFrames = true
let tracker = Tracker()
output.setSampleBufferDelegate(tracker, queue: DispatchQueue(label: "face-track"))
guard session.canAddOutput(output) else { exit(1) }
session.addOutput(output)
session.startRunning()

sendTime()
Timer.scheduledTimer(withTimeInterval: 60, repeats: true) { _ in sendTime() }

print("face-track: \(cam.localizedName) -> \(portPath)  (Ctrl-C to quit)")
RunLoop.main.run()
