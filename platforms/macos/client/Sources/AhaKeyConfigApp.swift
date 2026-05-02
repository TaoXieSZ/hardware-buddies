import AppKit
import SwiftUI
import Combine

extension Notification.Name {
    /// Debug：在 Xcode 运行（⌘R）时由菜单触发，在 `ContentView` 中以 sheet 展示统一引导，便于不依赖 Canvas 预览 UI。
    static let ahaKeyDebugShowOnboardingPreview = Notification.Name("AhaKeyDebugShowOnboardingPreview")
}

@main
struct AhaKeyConfigApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) var appDelegate
    @StateObject private var bleManager = AhaKeyBLEManager()

    var body: some Scene {
        Window("AhaKey Studio", id: "main") {
            ContentView(bleManager: bleManager)
                .frame(minWidth: 1280, minHeight: 820)
        }
        .windowStyle(.titleBar)
        .windowResizability(.contentMinSize)
        #if DEBUG
        .commands {
            CommandMenu("调试") {
                Button("预览统一引导…") {
                    NotificationCenter.default.post(name: .ahaKeyDebugShowOnboardingPreview, object: nil)
                }
                .keyboardShortcut("p", modifiers: [.command, .option])
            }
        }
        #endif

        MenuBarExtra("AhaKey", systemImage: "keyboard") {
            Button("打开主窗口") {
                appDelegate.reopenMainWindow()
            }

            Divider()

            Button("退出 AhaKey Studio") {
                NSApp.terminate(nil)
            }
        }
    }
}

@MainActor
final class AppDelegate: NSObject, NSApplicationDelegate {
    private var voiceHUDPanel: NSPanel?
    private var voiceHUDHostingView: NSHostingView<VoiceInputFloatingHUD>?
    private var cancellables = Set<AnyCancellable>()
    private var lastHUDCommittedText = ""
    private var voiceHUDUserFrameOrigin: NSPoint?
    private var voiceHUDWasVisible = false

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool {
        false
    }

    func applicationDidFinishLaunching(_ notification: Notification) {
        // 单实例：检查是否已有实例在运行
        let bundleID = Bundle.main.bundleIdentifier ?? "lab.jawa.ahakeyconfig"
        let running = NSRunningApplication.runningApplications(withBundleIdentifier: bundleID)
        if running.count > 1 {
            if let existing = running.first(where: { $0 != NSRunningApplication.current }) {
                existing.activate()
            }
            NSApp.terminate(nil)
        }

        VoiceRelayService.shared.start()
        NativeSpeechTranscriptionService.shared.start()
        VoiceAgentSessionStore.shared.start()
        installVoiceHUDPanel()
        observeVoiceHUDVisibility()
    }

    func applicationDidBecomeActive(_ notification: Notification) {
        VoiceRelayService.shared.refreshPermissions()
        NativeSpeechTranscriptionService.shared.refreshPermissions()
    }

    func applicationShouldHandleReopen(_ sender: NSApplication, hasVisibleWindows flag: Bool) -> Bool {
        if !flag {
            reopenMainWindow()
        }
        return true
    }

    func reopenMainWindow() {
        NSApp.activate(ignoringOtherApps: true)
        if let mainWindow = NSApp.windows.first {
            mainWindow.makeKeyAndOrderFront(nil)
        }
    }

    private func installVoiceHUDPanel() {
        guard voiceHUDPanel == nil else { return }

        let nativeSpeech = NativeSpeechTranscriptionService.shared
        let view = VoiceInputFloatingHUD(
            nativeSpeech: nativeSpeech,
            onCancel: {
                if nativeSpeech.isRecording {
                    nativeSpeech.stopRecording()
                }
            },
            onConfirm: {
                if nativeSpeech.isRecording {
                    nativeSpeech.stopRecording()
                }
            },
            onDragChanged: { [weak self] delta in
                self?.moveVoiceHUDPanel(by: delta)
            },
            onDragEnded: { [weak self] in
                self?.rememberVoiceHUDPanelPosition()
            }
        )
        let hostingView = NSHostingView(rootView: view)
        hostingView.frame = NSRect(x: 0, y: 0, width: 180, height: 58)

        let panel = NSPanel(
            contentRect: hostingView.frame,
            styleMask: [.nonactivatingPanel, .fullSizeContentView],
            backing: .buffered,
            defer: false
        )
        panel.contentView = hostingView
        panel.isOpaque = false
        panel.backgroundColor = .clear
        panel.hasShadow = false
        panel.level = .floating
        panel.collectionBehavior = [.canJoinAllSpaces, .fullScreenAuxiliary, .stationary]
        panel.hidesOnDeactivate = false
        panel.ignoresMouseEvents = false
        panel.isReleasedWhenClosed = false
        panel.titleVisibility = .hidden
        panel.titlebarAppearsTransparent = true
        panel.standardWindowButton(.closeButton)?.isHidden = true
        panel.standardWindowButton(.miniaturizeButton)?.isHidden = true
        panel.standardWindowButton(.zoomButton)?.isHidden = true

        voiceHUDPanel = panel
        voiceHUDHostingView = hostingView
    }

    private func observeVoiceHUDVisibility() {
        let service = NativeSpeechTranscriptionService.shared
        Publishers.Merge3(
            service.$isRecording.map { _ in () }.eraseToAnyPublisher(),
            service.$statusMessage.map { _ in () }.eraseToAnyPublisher(),
            service.$lastCommittedText.map { _ in () }.eraseToAnyPublisher()
        )
        .receive(on: RunLoop.main)
        .sink { [weak self] in
            self?.updateVoiceHUDPanelVisibility()
        }
        .store(in: &cancellables)
    }

    private func updateVoiceHUDPanelVisibility() {
        guard let panel = voiceHUDPanel else { return }
        let service = NativeSpeechTranscriptionService.shared
        let isBusy = service.statusMessage.contains("整理")
        let hasNewCommittedText = !service.lastCommittedText.isEmpty && service.lastCommittedText != lastHUDCommittedText
        let visible = service.isRecording || isBusy || hasNewCommittedText

        if visible {
            if !voiceHUDWasVisible {
                positionVoiceHUDPanel(panel)
            }
            panel.orderFrontRegardless()
            voiceHUDWasVisible = true
            if hasNewCommittedText {
                lastHUDCommittedText = service.lastCommittedText
                DispatchQueue.main.asyncAfter(deadline: .now() + 1.35) { [weak self] in
                    guard let self, !NativeSpeechTranscriptionService.shared.isRecording else { return }
                    if !NativeSpeechTranscriptionService.shared.statusMessage.contains("整理") {
                        self.voiceHUDPanel?.orderOut(nil)
                        self.voiceHUDWasVisible = false
                    }
                }
            }
        } else {
            panel.orderOut(nil)
            voiceHUDWasVisible = false
        }
    }

    private func positionVoiceHUDPanel(_ panel: NSPanel) {
        let size = NSSize(width: 180, height: 58)
        if let savedOrigin = voiceHUDUserFrameOrigin {
            panel.setFrame(NSRect(origin: clampedVoiceHUDOrigin(savedOrigin, size: size), size: size), display: true)
            return
        }
        if let mainWindow = NSApp.windows.first(where: { $0.title == "AhaKey Studio" || $0.isMainWindow }) {
            let frame = mainWindow.frame
            let origin = NSPoint(
                x: frame.midX - size.width / 2,
                y: max(frame.minY + 18, (mainWindow.screen?.visibleFrame.minY ?? 0) + 18)
            )
            panel.setFrame(NSRect(origin: origin, size: size), display: true)
            return
        }

        let screen = NSScreen.main ?? NSScreen.screens.first
        let frame = screen?.visibleFrame ?? NSRect(x: 0, y: 0, width: 1280, height: 800)
        let origin = NSPoint(
            x: frame.midX - size.width / 2,
            y: frame.minY + 18
        )
        panel.setFrame(NSRect(origin: origin, size: size), display: true)
    }

    private func moveVoiceHUDPanel(by delta: CGSize) {
        guard let panel = voiceHUDPanel else { return }
        let current = panel.frame
        let nextOrigin = NSPoint(
            x: current.origin.x + delta.width,
            y: current.origin.y + delta.height
        )
        panel.setFrame(NSRect(origin: clampedVoiceHUDOrigin(nextOrigin, size: current.size), size: current.size), display: true)
    }

    private func rememberVoiceHUDPanelPosition() {
        guard let panel = voiceHUDPanel else { return }
        voiceHUDUserFrameOrigin = panel.frame.origin
    }

    private func clampedVoiceHUDOrigin(_ origin: NSPoint, size: NSSize) -> NSPoint {
        let screenFrame = (NSScreen.screens.first { $0.visibleFrame.intersects(NSRect(origin: origin, size: size)) } ?? NSScreen.main ?? NSScreen.screens.first)?.visibleFrame
            ?? NSRect(x: 0, y: 0, width: 1280, height: 800)
        return NSPoint(
            x: min(max(origin.x, screenFrame.minX + 8), screenFrame.maxX - size.width - 8),
            y: min(max(origin.y, screenFrame.minY + 8), screenFrame.maxY - size.height - 8)
        )
    }
}
