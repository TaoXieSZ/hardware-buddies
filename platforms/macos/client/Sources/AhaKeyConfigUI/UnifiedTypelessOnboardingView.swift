import SwiftUI

public enum UnifiedOnboardingStorage {
    public static let completedKey = "AhaKey.UnifiedOnboarding.v1.completed"
    public static let micGrantedKey = "AhaKey.UnifiedOnboarding.v1.micPreGranted"
    public static let pasteGrantedKey = "AhaKey.UnifiedOnboarding.v1.pastePreGranted"
}

public struct OnboardingPermissionState: Equatable {
    public var inputMonitoringGranted: Bool
    public var accessibilityGranted: Bool
    public var microphoneGranted: Bool
    public var speechRecognitionGranted: Bool
    public var voiceSummary: String
    public var speechSummary: String
    public var isRecording: Bool
    public var transcriptPreview: String
    public var lastCommittedText: String
    public var speechStatusMessage: String

    public init(
        inputMonitoringGranted: Bool,
        accessibilityGranted: Bool,
        microphoneGranted: Bool,
        speechRecognitionGranted: Bool,
        voiceSummary: String,
        speechSummary: String,
        isRecording: Bool,
        transcriptPreview: String,
        lastCommittedText: String,
        speechStatusMessage: String
    ) {
        self.inputMonitoringGranted = inputMonitoringGranted
        self.accessibilityGranted = accessibilityGranted
        self.microphoneGranted = microphoneGranted
        self.speechRecognitionGranted = speechRecognitionGranted
        self.voiceSummary = voiceSummary
        self.speechSummary = speechSummary
        self.isRecording = isRecording
        self.transcriptPreview = transcriptPreview
        self.lastCommittedText = lastCommittedText
        self.speechStatusMessage = speechStatusMessage
    }

    public var corePermissionsGranted: Bool {
        microphoneGranted && speechRecognitionGranted
    }

    public var backgroundPermissionsGranted: Bool {
        inputMonitoringGranted && accessibilityGranted
    }

    public var allPermissionsGranted: Bool {
        corePermissionsGranted && backgroundPermissionsGranted
    }

    public var statusLooksBusy: Bool {
        speechStatusMessage.contains("整理") || speechStatusMessage.contains("写入")
    }
}

public struct OnboardingPermissionActions {
    public var requestPermissions: () -> Void
    public var recheckPermissions: () -> Void
    public var openSystemSettings: () -> Void
    public var toggleTryExperience: () -> Void

    public init(
        requestPermissions: @escaping () -> Void,
        recheckPermissions: @escaping () -> Void,
        openSystemSettings: @escaping () -> Void,
        toggleTryExperience: @escaping () -> Void
    ) {
        self.requestPermissions = requestPermissions
        self.recheckPermissions = recheckPermissions
        self.openSystemSettings = openSystemSettings
        self.toggleTryExperience = toggleTryExperience
    }
}

public struct UnifiedTypelessOnboardingView: View {
    public var permissionState: OnboardingPermissionState
    public var actions: OnboardingPermissionActions
    public var onCompleted: (_ micGranted: Bool, _ pasteGranted: Bool) -> Void

    public init(
        permissionState: OnboardingPermissionState,
        actions: OnboardingPermissionActions,
        onCompleted: @escaping (_ micGranted: Bool, _ pasteGranted: Bool) -> Void
    ) {
        self.permissionState = permissionState
        self.actions = actions
        self.onCompleted = onCompleted
    }

    @State private var step: TourStep = .register
    @State private var didRunTryExperience = false

    private enum Typography {
        static let hero = Font.system(size: 30, weight: .semibold)
        static let title = Font.system(size: 26, weight: .semibold)
        static let section = Font.system(size: 18, weight: .semibold)
        static let body = AhaKeyUI.Font.body
        static let detail = AhaKeyUI.Font.footnote
        static let caption = AhaKeyUI.Font.caption
    }

    public var body: some View {
        GeometryReader { geometry in
            let compact = geometry.size.width < 980
            let mainWidth = geometry.size.width * 0.50
            let verticalPadding: CGFloat = geometry.size.height < 760 ? 36 : 52
            VStack(spacing: 0) {
                topBar
                Divider().opacity(0.35)
                if compact {
                    ScrollView {
                        VStack(spacing: 20) {
                            mainColumn(compact: true)
                            supportColumn(compact: true)
                        }
                        .padding(.horizontal, 24)
                        .padding(.vertical, 24)
                    }
                } else {
                    HStack(spacing: 0) {
                        mainColumn(compact: false)
                            .frame(width: mainWidth, alignment: .leading)
                            .padding(.horizontal, 60)
                            .padding(.vertical, verticalPadding)
                            .background(Color(nsColor: .textBackgroundColor))

                        Divider().opacity(0.35)

                        supportColumn(compact: false)
                            .frame(maxWidth: .infinity, maxHeight: .infinity)
                            .padding(.horizontal, 56)
                            .padding(.vertical, verticalPadding)
                            .background(onboardingSupportBackground)
                    }
                }
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .background(AhaKeyUI.ColorToken.base)
        }
        .onChange(of: permissionState.transcriptPreview) { _, newValue in
            if !newValue.isEmpty { didRunTryExperience = true }
        }
        .onChange(of: permissionState.lastCommittedText) { _, newValue in
            if !newValue.isEmpty { didRunTryExperience = true }
        }
    }

    private var topBar: some View {
        HStack(spacing: 16) {
            Spacer(minLength: 0)
            stepper
            Spacer(minLength: 0)
            Button("跳过") { finish(skip: true) }
                .buttonStyle(.borderless)
                .foregroundStyle(.secondary)
                .padding(.trailing, 18)
        }
        .padding(.top, 10)
        .padding(.bottom, 8)
        .background(Color(nsColor: .textBackgroundColor))
    }

    private var stepper: some View {
        HStack(spacing: 14) {
            ForEach(TourStep.allCases) { item in
                HStack(spacing: 14) {
                    Button {
                        if item.rawValue <= step.rawValue { step = item }
                    } label: {
                        Text(item.title)
                            .font(AhaKeyUI.Font.title3.weight(step == item ? .semibold : .medium))
                            .foregroundStyle(step == item ? Color.primary : Color.secondary)
                            .frame(width: 84, height: 38)
                            .contentShape(Rectangle())
                            .overlay(alignment: .bottom) {
                                Rectangle()
                                    .fill(step == item ? Color.primary : Color.clear)
                                    .frame(height: 2)
                            }
                    }
                    .buttonStyle(.plain)
                    .disabled(item.rawValue > step.rawValue)

                    if item != TourStep.allCases.last {
                        Image(systemName: "chevron.right")
                            .font(AhaKeyUI.Font.footnote.weight(.semibold))
                            .foregroundStyle(.tertiary)
                            .frame(width: 12, height: 38)
                            .accessibilityHidden(true)
                    }
                }
            }
        }
        .frame(height: 38)
    }

    private func mainColumn(compact: Bool) -> some View {
        VStack(alignment: .leading, spacing: 0) {
            if step != .register {
                Button {
                    step = step.previous
                } label: {
                    Label("返回", systemImage: "chevron.left")
                        .font(AhaKeyUI.Font.subhead)
                        .foregroundStyle(.secondary)
                }
                .buttonStyle(.plain)
                .padding(.bottom, 16)
            }

            Group {
                switch step {
                case .register:
                    registerPanel
                case .setup:
                    setupPanel
                case .tryIt:
                    tryPanel
                }
            }
            .frame(maxWidth: .infinity, alignment: .leading)

            if !compact {
                Spacer(minLength: 0)
            }
        }
        .frame(maxWidth: .infinity, maxHeight: compact ? nil : .infinity, alignment: .topLeading)
    }

    private var registerPanel: some View {
        VStack(alignment: .leading, spacing: 0) {
            Text("在您的计算机上设置 AhaKey")
                .font(Typography.hero)
                .foregroundStyle(.primary)
                .lineLimit(2)
                .minimumScaleFactor(0.86)

            Spacer(minLength: 64)

            VStack(alignment: .leading, spacing: 16) {
                onboardingActionCard(
                    title: "允许 AhaKey 将文本粘贴到任何文本框中",
                    detail: "这让 AhaKey 能够将您的口述内容放入正确的文本框中。",
                    buttonTitle: permissionState.backgroundPermissionsGranted ? "已允许" : "允许",
                    showsInfo: true,
                    granted: permissionState.backgroundPermissionsGranted,
                    action: actions.requestPermissions
                )
                onboardingActionCard(
                    title: "允许 AhaKey 使用您的麦克风",
                    detail: "语音输入需要麦克风权限才能开始聆听。",
                    buttonTitle: permissionState.microphoneGranted ? "已允许" : "允许",
                    showsInfo: false,
                    granted: permissionState.microphoneGranted,
                    action: actions.requestPermissions
                )
            }

            Spacer(minLength: 34)

            HStack(spacing: 12) {
                Button("打开系统设置") { actions.openSystemSettings() }
                    .buttonStyle(OnboardingSecondaryButtonStyle())
                Spacer()
                Button("继续") {
                    actions.recheckPermissions()
                    step = .setup
                }
                .buttonStyle(OnboardingPrimaryButtonStyle())
            }
        }
    }

    private var setupPanel: some View {
        VStack(alignment: .leading, spacing: 16) {
            Text("在本机完成语音设置")
                .font(Typography.title)
            Text("AhaKey 需要 macOS 权限才能在后台监听语音键、调用系统语音识别，并把结果插入当前输入位置。")
                .font(Typography.body)
                .foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)

            VStack(alignment: .leading, spacing: 10) {
                permissionRow(
                    title: "麦克风",
                    detail: "用于采集语音输入。",
                    granted: permissionState.microphoneGranted,
                    required: true
                )
                permissionRow(
                    title: "语音识别",
                    detail: "用于把语音转为文字。",
                    granted: permissionState.speechRecognitionGranted,
                    required: true
                )
                permissionRow(
                    title: "输入监控",
                    detail: "用于后台监听实体语音键。",
                    granted: permissionState.inputMonitoringGranted,
                    required: false
                )
                permissionRow(
                    title: "辅助功能",
                    detail: "用于把语音键转换为系统输入动作。",
                    granted: permissionState.accessibilityGranted,
                    required: false
                )
            }

            VStack(alignment: .leading, spacing: 4) {
                Text(permissionState.speechSummary)
                Text(permissionState.voiceSummary)
            }
            .font(AhaKeyUI.Font.caption)
            .foregroundStyle(.secondary)
            .fixedSize(horizontal: false, vertical: true)

            HStack(spacing: 8) {
                Button("申请权限") { actions.requestPermissions() }
                    .buttonStyle(OnboardingPrimaryButtonStyle())
                Button("重新检查") { actions.recheckPermissions() }
                    .buttonStyle(OnboardingSecondaryButtonStyle())
                Button("打开系统设置") { actions.openSystemSettings() }
                    .buttonStyle(OnboardingSecondaryButtonStyle())
            }
            .fixedSize(horizontal: false, vertical: true)
            .padding(.top, 2)

            setupNotice

            Spacer(minLength: 18)
            primaryAction(permissionState.corePermissionsGranted ? "继续体验" : "先跳过并体验模拟") {
                step = .tryIt
            }
        }
    }

    private var setupNotice: some View {
        HStack(alignment: .top, spacing: 10) {
            Image(systemName: "info.circle.fill")
                .foregroundStyle(Color.blue)
            Text("麦克风和语音识别是体验核心功能的必需权限；输入监控和辅助功能用于实体语音键后台触发，可稍后补齐。")
                .font(Typography.caption)
                .foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)
        }
        .ahaKeyRemarkPanel()
    }

    private var tryPanel: some View {
        VStack(alignment: .leading, spacing: 22) {
            Text("口述以测试您的麦克风")
                .font(Typography.hero)
            Text("您计算机内置的麦克风将确保最佳的转录效果。")
                .font(Typography.body)
                .foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)

            Spacer(minLength: 96)

            Text("您在说话时看到蓝色条形图在移动吗？")
                .font(Typography.section)
                .foregroundStyle(.primary)
                .fixedSize(horizontal: false, vertical: true)

            if !permissionState.corePermissionsGranted {
                Text("麦克风或语音识别权限未完成。可以先回到设置开启权限，或继续进入工作台稍后再试。")
                    .font(Typography.detail)
                    .foregroundStyle(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
            } else if !tryPreviewText.isEmpty, tryPreviewText != placeholderPreview {
                Text("识别预览：\(tryPreviewText)")
                    .font(Typography.detail)
                    .foregroundStyle(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
            }

            Spacer(minLength: 18)

            HStack(spacing: 14) {
                Spacer()
                Button("不，换个麦克风") {
                    step = .setup
                }
                .buttonStyle(OnboardingSecondaryButtonStyle())
                Button(permissionState.corePermissionsGranted ? (permissionState.isRecording ? "结束录音" : "是的，继续") : "进入工作台") {
                    if permissionState.corePermissionsGranted && !permissionState.isRecording && !didRunTryExperience {
                        didRunTryExperience = true
                        actions.toggleTryExperience()
                    } else if permissionState.isRecording {
                        didRunTryExperience = true
                        actions.toggleTryExperience()
                    } else {
                        finish(skip: false)
                    }
                }
                .buttonStyle(OnboardingPrimaryButtonStyle())
            }
        }
    }

    private var simulatedExperiencePanel: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("模拟体验流程")
                .font(AhaKeyUI.Font.subhead.weight(.semibold))
            Text("按下语音键 → AhaKey 开始聆听 → 系统语音识别 → 文本写入当前光标。")
                .font(AhaKeyUI.Font.footnote)
                .foregroundStyle(.secondary)
        }
        .ahaKeyRemarkPanel()
    }

    private func supportColumn(compact: Bool) -> some View {
        VStack(alignment: .leading, spacing: 16) {
            switch step {
            case .register:
                macPermissionIllustration
            case .setup:
                privacyCard
            case .tryIt:
                trySupportCard
            }
            if !compact {
                Spacer(minLength: 0)
            }
        }
        .frame(maxWidth: compact ? .infinity : 480, maxHeight: compact ? nil : .infinity, alignment: .center)
    }

    private var privacyCard: some View {
        VStack(alignment: .leading, spacing: 24) {
            privacyRow("零云数据保留", "您的语音输入是私密的，且没有数据保留。", "lock")
            privacyRow("从不训练您的数据", "您的任何输入数据都不会被我们或第三方存储或用于模型训练。", "nosign")
            privacyRow("设备内历史记录存储", "所有历史记录都保留在您的设备上。", "laptopcomputer")
        }
        .padding(34)
        .ahaKeySurface()
    }

    private var macPermissionIllustration: some View {
        VStack(spacing: 18) {
            Spacer(minLength: 0)
            ZStack {
                RoundedRectangle(cornerRadius: 14, style: .continuous)
                    .fill(Color(nsColor: .windowBackgroundColor).opacity(0.78))
                    .overlay(
                        RoundedRectangle(cornerRadius: 14, style: .continuous)
                            .strokeBorder(Color.black.opacity(0.08), lineWidth: 1)
                    )
                    .shadow(color: .black.opacity(0.12), radius: 22, y: 10)

                VStack(spacing: 13) {
                    ZStack(alignment: .bottomTrailing) {
                        Image(systemName: "touchid")
                            .font(.system(size: 44, weight: .regular))
                            .foregroundStyle(
                                LinearGradient(
                                    colors: [Color.pink, Color.purple, Color.blue],
                                    startPoint: .topLeading,
                                    endPoint: .bottomTrailing
                                )
                            )
                        Image(systemName: "hand.raised.fill")
                            .font(.system(size: 14, weight: .semibold))
                            .foregroundStyle(.white)
                            .frame(width: 24, height: 24)
                            .background(Color.blue, in: RoundedRectangle(cornerRadius: 7, style: .continuous))
                            .offset(x: 8, y: 5)
                    }

                    Text("Privacy & Security")
                        .font(.system(size: 16, weight: .semibold))

                    Text("Privacy & Security is trying to modify your system settings.")
                        .font(.system(size: 12, weight: .medium))
                        .foregroundStyle(.secondary)
                        .multilineTextAlignment(.center)
                        .lineLimit(2)

                    Text("Touch ID or enter your password to allow this.")
                        .font(.system(size: 12))
                        .foregroundStyle(.secondary)
                        .multilineTextAlignment(.center)

                    VStack(spacing: 7) {
                        RoundedRectangle(cornerRadius: 6, style: .continuous)
                            .fill(Color.blue)
                            .frame(height: 31)
                            .overlay {
                                Text("Use Password...")
                                    .font(.system(size: 13, weight: .semibold))
                                    .foregroundStyle(.white)
                            }
                        RoundedRectangle(cornerRadius: 6, style: .continuous)
                            .fill(Color(nsColor: .controlBackgroundColor))
                            .frame(height: 31)
                            .overlay {
                                Text("Cancel")
                                    .font(.system(size: 13, weight: .medium))
                                    .foregroundStyle(.primary)
                            }
                    }
                    .padding(.top, 2)
                }
                .padding(30)
                .frame(width: 260)
                .background(
                    RoundedRectangle(cornerRadius: 12, style: .continuous)
                        .fill(.regularMaterial)
                        .shadow(color: .black.opacity(0.16), radius: 16, y: 8)
                )
            }
            .frame(maxWidth: 470, minHeight: 320)
            .aspectRatio(1.32, contentMode: .fit)

            Text("macOS 会在权限变更时要求您确认。AhaKey 只使用这些权限完成本机语音输入，不上传您的语音内容。")
                .font(AhaKeyUI.Font.caption)
                .foregroundStyle(.secondary)
                .multilineTextAlignment(.center)
                .fixedSize(horizontal: false, vertical: true)
                .frame(maxWidth: 420)
            Spacer(minLength: 0)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }

    private var trySupportCard: some View {
        VStack(spacing: 18) {
            MicLevelMeterView(phase: Date().timeIntervalSinceReferenceDate)
                .frame(maxWidth: .infinity, alignment: .center)
            if permissionState.isRecording {
                FloatingVoiceBallPreview(mode: .recording)
            } else if permissionState.statusLooksBusy {
                FloatingVoiceBallPreview(mode: .thinking)
            } else {
                FloatingVoiceBallPreview(mode: .recording)
                    .opacity(0.72)
            }
        }
        .padding(.horizontal, 42)
        .padding(.vertical, 28)
        .ahaKeySurface()
    }

    private func permissionRow(title: String, detail: String, granted: Bool, required: Bool) -> some View {
        HStack(alignment: .top, spacing: 12) {
            Image(systemName: granted ? "checkmark.circle.fill" : "exclamationmark.circle.fill")
                .foregroundStyle(granted ? Color.green : Color.orange)
                .frame(width: 18)
            VStack(alignment: .leading, spacing: 4) {
                HStack(spacing: 8) {
                    Text(title)
                        .font(AhaKeyUI.Font.subhead.weight(.semibold))
                    Text(required ? "必需" : "增强")
                        .font(AhaKeyUI.Font.caption.weight(.semibold))
                        .foregroundStyle(required ? Color.blue : Color.secondary)
                    Spacer()
                    Text(granted ? "已开启" : "未开启")
                        .font(AhaKeyUI.Font.caption.weight(.semibold))
                        .foregroundStyle(granted ? Color.green : Color.orange)
                }
                Text(detail)
                    .font(AhaKeyUI.Font.caption)
                    .foregroundStyle(.secondary)
            }
        }
        .padding(12)
        .background(
            RoundedRectangle(cornerRadius: AhaKeyUI.Radius.medium, style: .continuous)
                .fill(AhaKeyUI.ColorToken.control)
        )
        .overlay(
            RoundedRectangle(cornerRadius: AhaKeyUI.Radius.medium, style: .continuous)
                .strokeBorder(AhaKeyUI.ColorToken.border, lineWidth: 1)
        )
    }

    private func onboardingActionCard(
        title: String,
        detail: String,
        buttonTitle: String,
        showsInfo: Bool,
        granted: Bool,
        action: @escaping () -> Void
    ) -> some View {
        VStack(alignment: .leading, spacing: 22) {
            VStack(alignment: .leading, spacing: 12) {
                Text(title)
                    .font(Typography.section)
                    .foregroundStyle(.primary)
                    .fixedSize(horizontal: false, vertical: true)
                if !detail.isEmpty {
                    Text(detail)
                        .font(Typography.body)
                        .foregroundStyle(.secondary)
                        .fixedSize(horizontal: false, vertical: true)
                }
            }

            HStack(spacing: 14) {
                Button(buttonTitle, action: action)
                    .buttonStyle(OnboardingPrimaryButtonStyle())
                    .disabled(granted)

                if showsInfo {
                    Button {
                        actions.openSystemSettings()
                    } label: {
                        Image(systemName: "info.circle")
                            .font(.system(size: 18, weight: .medium))
                            .foregroundStyle(.secondary)
                            .frame(width: 34, height: 34)
                            .background(Color(nsColor: .textBackgroundColor).opacity(0.65), in: Circle())
                    }
                    .buttonStyle(.plain)
                    .accessibilityLabel("查看权限说明")
                }
            }
        }
        .padding(.horizontal, 28)
        .padding(.vertical, 24)
        .frame(maxWidth: .infinity, minHeight: 118, alignment: .leading)
        .background(
            RoundedRectangle(cornerRadius: 18, style: .continuous)
                .fill(Color(nsColor: .controlBackgroundColor).opacity(0.55))
        )
    }

    private var onboardingSupportBackground: some View {
        Color(nsColor: .controlBackgroundColor).opacity(0.48)
    }

    private func featureRow(_ title: String, _ body: String, _ systemImage: String) -> some View {
        HStack(alignment: .top, spacing: 12) {
            Image(systemName: systemImage)
                .font(AhaKeyUI.Font.title3)
                .foregroundStyle(AhaKeyUI.ColorToken.primary)
                .frame(width: 32, height: 32)
                .background(
                    RoundedRectangle(cornerRadius: AhaKeyUI.Radius.small, style: .continuous)
                        .fill(AhaKeyUI.ColorToken.primary.opacity(0.12))
                )
            VStack(alignment: .leading, spacing: 4) {
                Text(title)
                    .font(AhaKeyUI.Font.subhead.weight(.semibold))
                Text(body)
                    .font(AhaKeyUI.Font.caption)
                    .foregroundStyle(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
            }
        }
    }

    private func privacyRow(_ title: String, _ body: String, _ systemImage: String) -> some View {
        featureRow(title, body, systemImage)
    }

    private func primaryAction(_ title: String, action: @escaping () -> Void) -> some View {
        HStack {
            Spacer()
            Button(title, action: action)
                .buttonStyle(OnboardingPrimaryButtonStyle())
        }
    }

    private var tryStatusTitle: String {
        if permissionState.isRecording { return "正在聆听" }
        if !permissionState.corePermissionsGranted { return "等待权限" }
        if !permissionState.transcriptPreview.isEmpty || !permissionState.lastCommittedText.isEmpty { return "已获得文字" }
        return "准备试说"
    }

    private let placeholderPreview = "语音识别结果会显示在这里。"

    private var tryPreviewText: String {
        if !permissionState.transcriptPreview.isEmpty { return permissionState.transcriptPreview }
        if !permissionState.lastCommittedText.isEmpty { return permissionState.lastCommittedText }
        if !permissionState.corePermissionsGranted { return "权限未完成时，这里展示模拟流程。开启麦克风和语音识别后可直接试说。" }
        return placeholderPreview
    }

    private enum FloatingVoiceBallMode {
        case recording
        case thinking
    }

    private struct FloatingVoiceBallPreview: View {
        var mode: FloatingVoiceBallMode

        var body: some View {
            Group {
                switch mode {
                case .recording:
                    HStack(spacing: 10) {
                        Image(systemName: "xmark")
                            .font(.system(size: 13, weight: .bold))
                            .foregroundStyle(.white)
                            .frame(width: 28, height: 28)
                            .background(Color.white.opacity(0.16), in: Circle())
                        MiniOnboardingVoiceWave()
                            .frame(width: 64, height: 24)
                        Image(systemName: "checkmark")
                            .font(.system(size: 13, weight: .bold))
                            .foregroundStyle(.black)
                            .frame(width: 28, height: 28)
                            .background(Color.white, in: Circle())
                    }
                    .padding(.horizontal, 8)
                    .padding(.vertical, 6)
                    .background(.black.opacity(0.92), in: Capsule())
                case .thinking:
                    Text("Thinking")
                        .font(.system(size: 15, weight: .medium))
                        .foregroundStyle(.white.opacity(0.92))
                        .padding(.horizontal, 18)
                        .padding(.vertical, 9)
                        .background(.black.opacity(0.74), in: Capsule())
                }
            }
            .overlay(Capsule().strokeBorder(.white.opacity(0.14), lineWidth: 1))
            .shadow(color: .black.opacity(0.22), radius: 14, y: 7)
        }
    }

    private struct MiniOnboardingVoiceWave: View {
        var body: some View {
            TimelineView(.animation(minimumInterval: 1.0 / 24.0)) { context in
                let t = context.date.timeIntervalSinceReferenceDate
                HStack(alignment: .center, spacing: 3) {
                    ForEach(0..<13, id: \.self) { index in
                        Capsule()
                            .fill(Color.white.opacity(index == 6 ? 1 : 0.86))
                            .frame(width: index == 6 ? 2 : 3, height: height(index: index, phase: t))
                    }
                }
            }
        }

        private func height(index: Int, phase: TimeInterval) -> CGFloat {
            if index == 6 { return 18 }
            let wave = sin(phase * 8 + Double(index) * 0.7) * 0.5 + 0.5
            return 5 + CGFloat(wave) * 14
        }
    }

    private func finish(skip: Bool) {
        _ = skip
        UserDefaults.standard.set(permissionState.microphoneGranted, forKey: UnifiedOnboardingStorage.micGrantedKey)
        UserDefaults.standard.set(permissionState.backgroundPermissionsGranted, forKey: UnifiedOnboardingStorage.pasteGrantedKey)
        onCompleted(permissionState.microphoneGranted, permissionState.backgroundPermissionsGranted)
    }
}

private enum TourStep: Int, CaseIterable, Identifiable {
    case register
    case setup
    case tryIt

    var id: Int { rawValue }

    var title: String {
        switch self {
        case .register: return "注册"
        case .setup: return "设置"
        case .tryIt: return "体验一下"
        }
    }

    var previous: TourStep {
        switch self {
        case .register: return .register
        case .setup: return .register
        case .tryIt: return .setup
        }
    }
}

private struct OnboardingPrimaryButtonStyle: ButtonStyle {
    @Environment(\.isEnabled) private var isEnabled
    @Environment(\.colorScheme) private var colorScheme

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .font(.system(size: 16, weight: .semibold))
            .foregroundStyle(foreground)
            .padding(.horizontal, 22)
            .frame(height: 38)
            .background(
                Capsule(style: .continuous)
                    .fill(buttonFill(isPressed: configuration.isPressed))
            )
            .scaleEffect(configuration.isPressed && isEnabled ? 0.985 : 1)
            .animation(.easeOut(duration: 0.12), value: configuration.isPressed)
    }

    private func buttonFill(isPressed: Bool) -> Color {
        if !isEnabled {
            return colorScheme == .dark ? Color.white.opacity(0.22) : Color.black.opacity(0.28)
        }
        if colorScheme == .dark {
            return Color.white.opacity(isPressed ? 0.78 : 0.92)
        }
        return Color.black.opacity(isPressed ? 0.78 : 0.92)
    }

    private var foreground: Color {
        if !isEnabled { return colorScheme == .dark ? Color.white.opacity(0.72) : Color.white }
        return colorScheme == .dark ? Color.black : Color.white
    }
}

private struct OnboardingSecondaryButtonStyle: ButtonStyle {
    @Environment(\.isEnabled) private var isEnabled

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .font(.system(size: 16, weight: .semibold))
            .foregroundStyle(isEnabled ? Color.primary : Color.secondary)
            .padding(.horizontal, 22)
            .frame(height: 38)
            .background(
                Capsule(style: .continuous)
                    .fill(configuration.isPressed ? AhaKeyUI.ColorToken.hover : Color(nsColor: .textBackgroundColor).opacity(0.72))
            )
            .overlay(
                Capsule(style: .continuous)
                    .strokeBorder(AhaKeyUI.ColorToken.borderStrong, lineWidth: 1)
            )
            .scaleEffect(configuration.isPressed && isEnabled ? 0.985 : 1)
            .animation(.easeOut(duration: 0.12), value: configuration.isPressed)
    }
}

private struct MicLevelMeterView: View {
    var phase: TimeInterval

    @Environment(\.accessibilityReduceMotion) private var reduceMotion

    var body: some View {
        TimelineView(.animation(minimumInterval: 1.0 / 30.0)) { context in
            let t = reduceMotion ? phase : context.date.timeIntervalSinceReferenceDate
            HStack(alignment: .bottom, spacing: 5) {
                ForEach(0..<7, id: \.self) { i in
                    RoundedRectangle(cornerRadius: 99, style: .continuous)
                        .fill(barColor(index: i, phase: t))
                        .frame(width: 8, height: barHeight(index: i, phase: t))
                }
            }
        }
        .padding(.horizontal, 20)
        .padding(.vertical, 12)
        .background(
            RoundedRectangle(cornerRadius: AhaKeyUI.Radius.medium, style: .continuous)
                .fill(AhaKeyUI.ColorToken.control)
        )
    }

    private func barHeight(index: Int, phase: TimeInterval) -> CGFloat {
        if reduceMotion { return 34 }
        let base = sin(phase * 5 + Double(index) * 0.55) * 0.5 + 0.5
        return CGFloat(14 + base * 42)
    }

    private func barColor(index: Int, phase: TimeInterval) -> Color {
        if reduceMotion { return Color.accentColor.opacity(0.85) }
        let base = sin(phase * 5 + Double(index) * 0.55) * 0.5 + 0.5
        return Color.accentColor.opacity(0.45 + base * 0.5)
    }
}

#if DEBUG
struct UnifiedTypelessOnboardingView_Previews: PreviewProvider {
    static var previews: some View {
        UnifiedTypelessOnboardingView(
            permissionState: .init(
                inputMonitoringGranted: false,
                accessibilityGranted: true,
                microphoneGranted: true,
                speechRecognitionGranted: false,
                voiceSummary: "输入监控未开启 · 辅助功能已开启",
                speechSummary: "麦克风已开启 · 语音识别未开启",
                isRecording: false,
                transcriptPreview: "",
                lastCommittedText: "",
                speechStatusMessage: "等待语音权限。"
            ),
            actions: .init(
                requestPermissions: {},
                recheckPermissions: {},
                openSystemSettings: {},
                toggleTryExperience: {}
            )
        ) { _, _ in }
        .frame(width: 1280, height: 820)
        .previewDisplayName("统一引导")
    }
}
#endif
