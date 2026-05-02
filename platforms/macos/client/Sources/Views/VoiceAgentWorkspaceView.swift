import SwiftUI
import VoiceAgent

struct VoiceAgentWorkspaceView<Header: View>: View {
    @StateObject private var assistantModel = VoiceAssistantModel.voiceAssistant()
    @StateObject private var nativeSpeech = NativeSpeechTranscriptionService.shared
    @State private var promptDraft = ""
    @State private var runSnapshots: [VoiceAgentRunSnapshot] = []
    @State private var selectedRunID: UUID?

    let modeEditorHeader: Header
    let onOpenConfiguration: () -> Void

    var body: some View {
        HStack(spacing: 0) {
            canvasPane
            Divider()
            inspectorPane
        }
        .background(Color(nsColor: .windowBackgroundColor))
        .onAppear {
            installVoicePromptConsumer()
        }
        .onDisappear {
            NativeSpeechTranscriptionService.shared.setFinalTranscriptConsumer(nil)
        }
        .task {
            await consumeRunEvents()
        }
    }

    // MARK: - Canvas (Left)

    private var canvasPane: some View {
        VStack(alignment: .leading, spacing: 0) {
            VStack(alignment: .leading, spacing: 18) {
                modeEditorHeader
                transcriptContent
            }
            .padding(24)
            .frame(maxWidth: .infinity, maxHeight: .infinity)

            voiceCaptureBanner
            Divider()
            promptBar
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }

    private var transcriptContent: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("Transcript")
                .font(.headline)

            ScrollView {
                LazyVStack(alignment: .leading, spacing: 10) {
                    ForEach(assistantModel.messages) { message in
                        transcriptRow(message)
                    }

                    if let error = assistantModel.lastError {
                        Text(error)
                            .font(.callout)
                            .foregroundStyle(.red)
                            .padding(12)
                            .frame(maxWidth: .infinity, alignment: .leading)
                            .background(
                                RoundedRectangle(cornerRadius: 8)
                                    .fill(Color.red.opacity(0.08))
                            )
                    }
                }
                .frame(maxWidth: .infinity, alignment: .leading)
                .padding(.vertical, 2)
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }

    // MARK: - Inspector (Right)

    private var inspectorPane: some View {
        VStack(alignment: .leading, spacing: 18) {
            inspectorHeader
            actionButtons

            if runSnapshots.isEmpty {
                emptyRunTree
            } else {
                ScrollView {
                    LazyVStack(alignment: .leading, spacing: 8) {
                        ForEach(orderedRuns) { run in
                            runRow(run)
                        }
                    }
                    .padding(.vertical, 2)
                }
            }
        }
        .padding(24)
        .frame(width: 390)
        .frame(maxHeight: .infinity)
        .background(Color(nsColor: .controlBackgroundColor).opacity(0.35))
    }

    private var inspectorHeader: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack(alignment: .top) {
                Label("Run Tree", systemImage: "list.bullet.indent")
                    .font(.system(size: 20, weight: .semibold))
                Spacer()
                Text("\(runSnapshots.count)")
                    .font(.caption.monospacedDigit())
                    .foregroundStyle(.secondary)
            }
            Text(runtimeStatusText)
                .font(.callout)
                .foregroundStyle(.secondary)
        }
    }

    private var actionButtons: some View {
        HStack(spacing: 8) {
            if assistantModel.isThinking {
                ProgressView()
                    .controlSize(.small)
            }

            Spacer()

            Button {
                Task { await resetSession() }
            } label: {
                Label("Reset", systemImage: "arrow.counterclockwise")
            }
            .disabled(assistantModel.isThinking)

            Button {
                onOpenConfiguration()
            } label: {
                Label("Settings", systemImage: "slider.horizontal.3")
            }
        }
    }

    // MARK: - Voice Capture Banner

    @ViewBuilder
    private var voiceCaptureBanner: some View {
        if nativeSpeech.isRecording || !nativeSpeech.transcriptPreview.isEmpty {
            HStack(alignment: .top, spacing: 10) {
                Circle()
                    .fill(nativeSpeech.isRecording ? Color.red : Color.secondary)
                    .frame(width: 8, height: 8)
                    .padding(.top, 5)
                    .opacity(nativeSpeech.isRecording ? 1.0 : 0.5)

                VStack(alignment: .leading, spacing: 4) {
                    Text(nativeSpeech.isRecording ? "正在听写…" : "整理文字…")
                        .font(.caption.weight(.semibold))
                        .foregroundStyle(.secondary)
                    if !nativeSpeech.transcriptPreview.isEmpty {
                        Text(nativeSpeech.transcriptPreview)
                            .font(.callout)
                            .foregroundStyle(.primary)
                            .textSelection(.enabled)
                    }
                }

                Spacer(minLength: 0)
            }
            .padding(12)
            .background(
                RoundedRectangle(cornerRadius: 8)
                    .fill(Color.red.opacity(nativeSpeech.isRecording ? 0.08 : 0.04))
            )
            .padding(.horizontal, 16)
            .padding(.top, 8)
            .transition(.opacity)
        }
    }

    // MARK: - Prompt Bar

    private var promptBar: some View {
        HStack(spacing: 12) {
            TextField("Send a prompt to the main agent", text: $promptDraft, axis: .vertical)
                .textFieldStyle(.roundedBorder)
                .lineLimit(1...4)
                .disabled(assistantModel.isThinking)
                .onSubmit {
                    Task { await sendPrompt() }
                }

            Button {
                Task { await sendPrompt() }
            } label: {
                Label("Send", systemImage: "paperplane.fill")
            }
            .buttonStyle(.borderedProminent)
            .disabled(assistantModel.isThinking || promptDraft.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty)
        }
        .padding(16)
        .background(Color(nsColor: .controlBackgroundColor).opacity(0.5))
    }

    // MARK: - Run Tree Helpers

    private var emptyRunTree: some View {
        VStack(alignment: .leading, spacing: 8) {
            Label("Idle", systemImage: "circle.dotted")
                .font(.callout.weight(.semibold))
                .foregroundStyle(.secondary)
            Text("No runs yet.")
                .font(.caption)
                .foregroundStyle(.secondary)
        }
        .padding(14)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(
            RoundedRectangle(cornerRadius: 8)
                .fill(Color(nsColor: .controlBackgroundColor))
        )
    }

    private func runRow(_ run: VoiceAgentRunSnapshot) -> some View {
        Button {
            selectedRunID = run.runID
        } label: {
            HStack(alignment: .top, spacing: 10) {
                Circle()
                    .fill(statusColor(run.status))
                    .frame(width: 9, height: 9)
                    .padding(.top, 5)

                VStack(alignment: .leading, spacing: 4) {
                    HStack(spacing: 6) {
                        Text(run.kind == .root ? "Main" : "Subagent")
                            .font(.caption.weight(.semibold))
                            .foregroundStyle(.secondary)
                        if !run.toolCalls.isEmpty {
                            Text("\(run.toolCalls.count) tools")
                                .font(.caption2)
                                .foregroundStyle(.secondary)
                        }
                    }
                    Text(run.title)
                        .font(.callout.weight(.semibold))
                        .foregroundStyle(.primary)
                        .lineLimit(2)
                    if let output = run.output, !output.isEmpty {
                        Text(output)
                            .font(.caption)
                            .foregroundStyle(.secondary)
                            .lineLimit(2)
                    } else if let error = run.error {
                        Text(error)
                            .font(.caption)
                            .foregroundStyle(.red)
                            .lineLimit(2)
                    }
                }
                Spacer(minLength: 0)
            }
            .padding(.leading, CGFloat(run.depth) * 18)
            .padding(10)
            .frame(maxWidth: .infinity, alignment: .leading)
            .background(
                RoundedRectangle(cornerRadius: 8)
                    .fill(selectedRunID == run.runID ? Color.accentColor.opacity(0.13) : Color(nsColor: .controlBackgroundColor))
            )
        }
        .buttonStyle(.plain)
    }

    private func transcriptRow(_ message: VoiceAssistantModel.ChatMessage) -> some View {
        VStack(alignment: .leading, spacing: 5) {
            Text(message.role.rawValue.capitalized)
                .font(.caption.weight(.semibold))
                .foregroundStyle(.secondary)
            Text(message.content)
                .font(.callout)
                .textSelection(.enabled)
                .frame(maxWidth: .infinity, alignment: .leading)
        }
        .padding(12)
        .background(
            RoundedRectangle(cornerRadius: 8)
                .fill(message.role == .user ? Color.accentColor.opacity(0.12) : Color(nsColor: .controlBackgroundColor))
        )
    }

    // MARK: - Logic

    private var orderedRuns: [VoiceAgentRunSnapshot] {
        runSnapshots.sorted {
            if $0.startedAt == $1.startedAt {
                return $0.runID.uuidString < $1.runID.uuidString
            }
            return $0.startedAt < $1.startedAt
        }
    }

    private var runtimeStatusText: String {
        if VoiceAgentRuntimeConfig.openAIAPIKey == nil {
            return "Keychain API key not found"
        }
        if assistantModel.isThinking {
            return "Running"
        }
        return "Ready"
    }

    private func sendPrompt() async {
        let text = promptDraft.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !text.isEmpty else { return }
        promptDraft = ""
        await assistantModel.send(text)
        runSnapshots = await assistantModel.runSnapshots()
        selectedRunID = runSnapshots.last?.runID
    }

    private func resetSession() async {
        await assistantModel.reset()
        runSnapshots = []
        selectedRunID = nil
    }

    private func installVoicePromptConsumer() {
        NativeSpeechTranscriptionService.shared.setFinalTranscriptConsumer { text in
            Task { @MainActor in
                await sendVoicePrompt(text)
            }
            return true
        }
    }

    private func sendVoicePrompt(_ text: String) async {
        let trimmed = text.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else { return }
        await assistantModel.send(trimmed)
        runSnapshots = await assistantModel.runSnapshots()
        selectedRunID = runSnapshots.last?.runID
    }

    private func consumeRunEvents() async {
        for await _ in assistantModel.runEvents {
            runSnapshots = await assistantModel.runSnapshots()
            if selectedRunID == nil {
                selectedRunID = runSnapshots.last?.runID
            }
        }
    }

    private func statusColor(_ status: VoiceAgentRunStatus) -> Color {
        switch status {
        case .running:
            .orange
        case .completed:
            .green
        case .failed:
            .red
        }
    }
}
