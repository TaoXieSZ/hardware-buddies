// ───────────────────────────────────────────────────────────────────────
// cardputer-claude-buddy — Cardputer-ADV 当 Claude Code 桌搭。
//
// cc-bridge(BLE NUS, 未加密 debug 服务)推会话状态 JSON → clawd 随真实状态动 +
// 会话计数角标;工具要审批时弹审批面板,键盘 ok/esc/a 拍板回送;tab 看会话列表。
//
// 协议/初始化均逐字核对 upstream(见各文件头):
//   BLE/NUS ← ../claude-code-buddy/src/ble_bridge.*  (ble_link.*)
//   状态 JSON/决定 ← ../claude-code-buddy/src/data.h + main.cpp  (cclink.*)
//   clawd 渲染 ← ../claude-code-buddy/src/character.cpp  (clawd_player.*)
// ───────────────────────────────────────────────────────────────────────
#include "M5Cardputer.h"
#include "agent_state.h"
#include "link_state.h"
#include "motion.h"
#include "clawd_player.h"
#include "cclink.h"
#include "sound_player.h"
#include "recorder.h"

static Motion g_motion;
static uint32_t g_lastMs = 0;
static constexpr uint32_t STILL_FOR_SLEEP = 180000;  // 3min 静止才睡；留足 idle/idle-reading 清醒窗口
static constexpr uint32_t APPROVAL_SAFETY_MS = 30000;  // 面板兜底超时(回落 ask)

// ── 自动息屏（背光层，独立于 sleep.gif）──────────────────────────────
// 无物理活动(按键 OR IMU 明显运动)超阈值 → setBrightness(0) 关背光护屏省电；
// 物理活动恢复到原亮度。SCREEN_OFF_MS < STILL_FOR_SLEEP(180s)：屏可能先于/独立于
// sleep.gif 态灭掉——没关系，背光层与 GIF 层解耦，sleep.gif 关背光时不可见、亮时立现。
// ⚠️ 活动/唤醒只认物理输入，绝不用 cclink::changed()（每心跳为真会导致永不息屏）。
static constexpr uint32_t SCREEN_OFF_MS = 60000;     // 1min 无按键且无 IMU 运动 → 熄屏
static uint8_t g_savedBrightness = 255;              // 息屏前的原亮度（setup 里用 getBrightness() 抓一次）
static bool g_screenOff = false;                     // 当前是否已息屏

static void screenOff() {
    if (g_screenOff) return;                         // 幂等，避免重复调
    M5Cardputer.Display.setBrightness(0);
    g_screenOff = true;
}
static void screenOn() {
    if (!g_screenOff) return;                        // 幂等
    M5Cardputer.Display.setBrightness(g_savedBrightness);  // 恢复原值，绝不硬编码 255
    g_screenOff = false;
}

// 审批跟踪
static char g_shownId[40] = {0};
static uint32_t g_promptShownMs = 0;

// 状态跟踪（边沿检测）
static bool g_wasOnline = false;
static bool g_wasFailed = false;   // 上帧 msg 是否 "failed:"（error reaction 边沿触发）

// 快捷 nudge：NORMAL 模式键 → 经 cmd:key 打进聚焦的 Claude 终端。
// keyName 非空 = 发命名键(如 escape/space)；否则 = 打 text + enter。
// 'h' 为特殊键（切换 HELP 覆盖层），不在此表中，在下方单独处理。
struct Nudge { char key; const char* text; const char* keyName; const char* label; };
static const Nudge NUDGES[] = {
    {'1', "continue",             nullptr,   "continue"},
    {'2', "run the tests",        nullptr,   "run tests"},
    {'3', "explain what you did", nullptr,   "explain"},
    {'4', nullptr,                "escape",  "stop"},
    {'5', "yes",                  nullptr,   "yes"},
    {'r', "try again",            nullptr,   "retry"},
    {'c', "commit the changes",   nullptr,   "commit"},
    {'f', "fix this",             nullptr,   "fix"},
    // 'v' 不在此表：改作 PTT hold-to-talk（按住=录音），见 loop() 的 PTT 轮询块。
};

void setup() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);        // true = 启用键盘
    M5Cardputer.Display.setRotation(1);  // 横屏 240x135
    Serial.begin(115200);

    g_savedBrightness = M5Cardputer.Display.getBrightness();  // 抓 begin 后的原亮度供息屏恢复
    g_motion.begin();
    clawd::begin();
    sound::begin();
    recorder::begin();
    cclink::begin();                     // 广播 Claude-XXXX,等 cc-bridge 连
    Serial.printf("[main] heap after init=%u\n", (unsigned)ESP.getFreeHeap());
    g_lastMs = millis();
}

void loop() {
    M5Cardputer.update();
    uint32_t now = millis();
    uint32_t dt = now - g_lastMs;
    g_lastMs = now;

    cclink::poll();
    const BuddyState& bs = cclink::state();
    bool online = cclink::connected();

    // 连接 / 断开音效
    if (online && !g_wasOnline) sound::play("connect");
    if (!online && g_wasOnline) sound::play("disconnect");
    g_wasOnline = online;

    // PTT hold-to-talk：按住 'v' → {cmd:mic,down}（Mac 听写开始录音），松开 → up（停）。
    // 每帧轮询 isKeyPressed('v')（不靠 isChange，可靠捕捉按住/松开）；'v' 不与任何覆盖层
    // 按键冲突，故无条件处理。down 需在线才发；松开总是清状态防 mic 卡住。
    static bool g_pttDown = false;
    bool vHeld = M5Cardputer.Keyboard.isKeyPressed('v');
    if (vHeld && !g_pttDown && online) {
        g_pttDown = true;
        cclink::sendMic(true);
        sound::play("nudge");
        clawd::setToast("REC...");
    } else if (!vHeld && g_pttDown) {
        g_pttDown = false;
        cclink::sendMic(false);
        clawd::setToast("voice sent");
    }

    // 本帧键盘事件读一次,按模式分发
    // 只用 isChange()，不要求 isPressed()——快速点击时 release 帧 isPressed() 已是 false 会漏键。
    // 释放帧 ks.word/enter/esc 均为空，不会双触发。
    bool keyEvent = M5Cardputer.Keyboard.isChange();
    Keyboard_Class::KeysState ks;
    if (keyEvent) ks = M5Cardputer.Keyboard.keysState();

    // 帧头快照——各模式块用快照，避免同帧 hide→show 状态竞争
    bool snapApproval = clawd::approvalVisible();
    bool snapQuestion = clawd::questionVisible();
    bool snapSessions = clawd::sessionsVisible();
    bool snapNotes    = clawd::notesVisible();
    bool snapNotebook = clawd::notebookVisible();
    bool snapHelp     = clawd::helpVisible();

    // ── 审批层(最高优先)──
    // AskUserQuestion 自带一个 permission prompt，但「回答问题」本身即审批。有 pending
    // question 时，自动放行(once)这个冗余审批、不弹审批层，让 QUESTION 层直接出——否则审批
    // 会盖住问答、逼用户先按一下才看到选项。复刻原本「once→答题」的有效流程，省掉手动 once。
    // 该 prompt 的 tool 名是默认 "tool"(daemon 对 AskUserQuestion 未带工具名)，不可靠；
    // 故以「有 pending question」为准——此时并发的这个 permission prompt 必是该问答的。
    // 自动放行(once)并记下 id，对该 id 永久抑制审批层：① question 答完后 prompt 常残留
    // 几十秒，避免它事后才弹出；② 让 QUESTION 层直接出，省掉手动 once。
    static char g_autoApprovedId[40] = {0};
    if (bs.promptId[0] && bs.hasQuestion && strcmp(bs.promptId, g_autoApprovedId) != 0) {
        strncpy(g_autoApprovedId, bs.promptId, sizeof(g_autoApprovedId) - 1);
        g_autoApprovedId[sizeof(g_autoApprovedId) - 1] = 0;
        cclink::sendDecision(bs.promptId, "once");   // 自动放行；真正的答案由 QUESTION 层回送
    }
    if (!bs.promptId[0]) g_autoApprovedId[0] = 0;
    bool hasPrompt = bs.promptId[0] != 0 && strcmp(bs.promptId, g_autoApprovedId) != 0;
    if (hasPrompt && strcmp(bs.promptId, g_shownId) != 0) {       // 新审批
        strncpy(g_shownId, bs.promptId, sizeof(g_shownId) - 1);
        g_shownId[sizeof(g_shownId) - 1] = 0;
        g_promptShownMs = now;
        clawd::showApproval(bs.promptTool, bs.promptHint);
    }
    if (snapApproval) {
        if (keyEvent) {
            // 串口诊断：审批态每次按键打印实际检测到的键（区分键没检测到 vs 没映射）。
            Serial.printf("[approval-key] ctrl=%d enter=%d esc=%d space=%d word=",
                          ks.ctrl, ks.enter, ks.esc, ks.space);
            for (char c : ks.word) Serial.printf("'%c'", c);
            Serial.println();

            const char* dec = nullptr;
            // 三角布局（键在键盘三个角，最不易误按）：
            //   空格(右下) = yes  /  esc键出'`'(左上) = no  /  ctrl(左下) = always
            // 注意: Cardputer 的 "esc" 键单按产出 backtick '`'（KEY_ESCAPE 在 fn 层）。
            // a/n 作后备（word 路径，已验证可靠）；enter/fn+esc 是 flag 路径（ADV 上不灵，留作兼容）。
            if (ks.ctrl)       dec = "always";   // ctrl 左下角 = always
            else if (ks.enter) dec = "once";
            else if (ks.esc)   dec = "deny";
            else for (auto c : ks.word) {
                if (c == ' ')                         { dec = "once";   break; }  // 空格右下角 = yes
                if (c == '`' || c == 'n' || c == 'N') { dec = "deny";   break; }  // esc键左上角 = no
                if (c == 'a' || c == 'A')             { dec = "always"; break; }  // a = always（后备）
            }
            if (dec) { cclink::sendDecision(g_shownId, dec); clawd::hideApproval(); }
        }
        // bridge 撤销 prompt(已解决/超时)或本地兜底超时 → 关面板(不发=ask)
        if (!hasPrompt || (now - g_promptShownMs > APPROVAL_SAFETY_MS)) clawd::hideApproval();
    }
    if (!hasPrompt) g_shownId[0] = 0;

    // ── AskUserQuestion 应答层(优先级次于审批，高于会话列表)──
    static char g_shownQRid[100] = {0};      // 当前已弹的 rid(防重复响铃/重置超时)
    static char g_dismissedQRid[100] = {0};  // 用户已答/取消的 rid(不再弹，直到 bridge 撤)
    static uint32_t g_qShownMs = 0;
    // 该问题仍待答(未被本机答过/取消过)。显示/恢复条件：未 dismiss、无更高层(审批)、
    // 当前没在显示 question —— 这样被 approval 覆盖后、approval 答完能自动恢复。
    bool qActive = bs.hasQuestion && strcmp(bs.question.rid, g_dismissedQRid) != 0;
    if (qActive && !snapApproval && !snapQuestion) {
        bool isNew = strcmp(bs.question.rid, g_shownQRid) != 0;
        strncpy(g_shownQRid, bs.question.rid, sizeof(g_shownQRid) - 1);
        g_shownQRid[sizeof(g_shownQRid) - 1] = 0;
        if (isNew) { g_qShownMs = now; sound::play("nudge"); }  // 仅真新问题响铃+计时(恢复不响)
        clawd::showQuestion(bs);
        snapQuestion = true;
    }
    if (snapQuestion) {
        if (keyEvent) {
            // canned 自由文本（走 Other 通道，见 change cardputer-question-chat-cancel）
            static const char* const kQChatText = "我想先聊聊这个，先别急着定 —— 能展开讲讲各选项吗？";
            static const char* const kQSkipText = "先跳过，你按最佳判断继续。";
            bool submit = ks.enter;
            bool cancel = ks.esc;            // fn+esc
            bool chat   = false;             // c 键：chat about it（回送讨论文本）
            for (auto c : ks.word) {
                if (c == '`')                  cancel = true;   // 单按 esc 键 = backtick
                else if (c == ' ')             submit = true;
                else if (c == 'c' || c == 'C') chat = true;     // chat about it
                else if (c == ',' || c == ';') clawd::questionMove(-1);
                else if (c == '.' || c == '/') clawd::questionMove(1);
                else if (c >= '1' && c <= '9') {
                    clawd::questionJumpTo(c - '1');
                    if (clawd::questionMulti()) clawd::questionToggle();  // 多选: toggle 勾选
                    else submit = true;                                  // 单选: 即选即交
                }
            }
            if (submit || cancel || chat) {
                const char* rid = clawd::questionRid();
                if (submit) {
                    const char* ids[6];
                    uint8_t nid = clawd::questionSelectedIds(ids, 6);
                    if (nid > 0) {
                        cclink::sendAnswerQuestion(rid, ids, nid);
                        clawd::setToast("answered");
                        sound::play("nudge");
                    }
                } else if (chat) {          // 「chat about it」：让 Claude 展开讨论而非干净选择
                    cclink::sendAnswerText(rid, kQChatText);
                    clawd::setToast("let's chat");
                    sound::play("nudge");
                } else {                    // 「cancel」：回送 skip 文本优雅解阻（取代旧的静默撤）
                    cclink::sendAnswerText(rid, kQSkipText);
                    clawd::setToast("skipped");
                    sound::play("nudge");
                }
                // 标记已处理：不再 resume，直到 bridge 撤(轮询发现不 pending)
                strncpy(g_dismissedQRid, bs.question.rid, sizeof(g_dismissedQRid) - 1);
                g_dismissedQRid[sizeof(g_dismissedQRid) - 1] = 0;
                clawd::hideQuestion();
            }
        }
        // 主要靠 daemon 撤（question 答了 / cmux 120s expired → hasQuestion=false）；
        // 本地超时延到 125s 对齐 cmux feed 的 120s 阻塞——AskUserQuestion 用户可能想很久，
        // 用审批的 30s 会过早撤面板、与 resume 打架造成闪烁。125s 仅极端兜底。
        if (!bs.hasQuestion || (now - g_qShownMs > 125000UL)) clawd::hideQuestion();
    }
    if (!bs.hasQuestion) { g_shownQRid[0] = 0; g_dismissedQRid[0] = 0; }

    // ── 会话列表(无审批/问题/笔记/笔记本时,tab 开关,esc 关,,/. 选,enter/space 切换)──
    if (!snapApproval && !snapQuestion && !snapNotes && !snapNotebook && keyEvent) {
        if (ks.tab) {
            if (snapSessions) clawd::hideSessions();
            else clawd::showSessions(bs);
        } else if (snapSessions) {
            bool confirm = ks.enter;   // 选中会话 → 切到它的终端
            bool close   = ks.esc;     // fn+esc 关
            for (auto c : ks.word) {
                if (c == '`')                    close = true;       // 单按 esc 键 = backtick
                else if (c == ' ')               confirm = true;     // space 也确认（与审批一致）
                else if (c == ',' || c == ';')   clawd::sessionsMove(-1);
                else if (c == '.' || c == '/')   clawd::sessionsMove(1);
            }
            if (confirm) {
                const char* sid = clawd::sessionsSelectedSid();
                if (sid && sid[0]) {
                    cclink::sendSelectSession(sid);
                    char t[24]; snprintf(t, sizeof(t), "switch %.8s", sid);
                    clawd::setToast(t);
                    sound::play("nudge");
                }
                clawd::hideSessions();
            } else if (close) {
                clawd::hideSessions();
            }
        }
    }

    // ── HELP 覆盖层(h 键切换,esc/backtick 关)──
    if (!snapApproval && !snapSessions && !snapNotes && !snapNotebook && keyEvent) {
        if (snapHelp) {
            if (ks.esc) { clawd::hideHelp(); }
            for (auto c : ks.word) {
                if (c == 'h' || c == 'H' || c == '`') { clawd::hideHelp(); break; }
            }
        }
    }

    // ── 笔记列表(无审批/问答/会话/帮助时,l 键在 NORMAL 块开关；列表内键盘独占)──
    if (keyEvent && !snapApproval && !snapQuestion && !snapSessions && !snapHelp) {
        if (snapNotes) {
            bool play  = ks.enter;
            bool close = ks.esc;
            bool del   = ks.backspace;     // 删选中笔记
            int  volD  = 0;                // 音量调整方向（-1=降 +1=升）
            for (auto c : ks.word) {
                if (c == '`')                  close = true;
                else if (c == ' ')             play = true;
                else if (c == ',' || c == ';') clawd::notesMove(-1);
                else if (c == '.' || c == '/') clawd::notesMove(1);
                else if (c == 'l' || c == 'L') { close = true; break; }
                else if (c == '-')             volD = -1;
                else if (c == '=')             volD = 1;
            }
            // 音量（优先于 close/play/del：同帧只做一项）
            if (volD != 0) {
                if (recorder::isPlaying()) {
                    recorder::adjVolume(volD * 25);
                } else {
                    if (volD > 0) sound::volumeUp(); else sound::volumeDown();
                }
                char t[16]; snprintf(t, sizeof(t), "vol %d", sound::volume());
                clawd::setToast(t);
            } else if (close) {
                if (recorder::isPlaying()) recorder::stopPlayback();
                clawd::hideNotes();
            } else if (del) {
                // 删选中笔记 → SD.remove → 刷新列表
                const char* sel = clawd::notesSelected();
                if (sel && sel[0]) {
                    // 正在播这个文件 → 先停再删
                    if (recorder::isPlaying()) recorder::stopPlayback();
                    if (recorder::deleteNote(sel)) {
                        clawd::setToast("deleted");
                    } else {
                        clawd::setToast("del fail");
                    }
                    // 刷新列表（可能删后变空或位置变）
                    char names[REC_MAX_NOTES][REC_NOTE_NAME_LEN];
                    uint8_t n = recorder::listNotes(names, REC_MAX_NOTES);
                    clawd::showNotes(names, n, -1);
                }
            } else if (play) {
                // 正在播 → 停止；否则播选中
                if (recorder::isPlaying()) {
                    recorder::stopPlayback();
                    // 回列表：刷新列表清除 ▶ 指示
                    char names[REC_MAX_NOTES][REC_NOTE_NAME_LEN];
                    uint8_t n = recorder::listNotes(names, REC_MAX_NOTES);
                    clawd::showNotes(names, n, -1);
                } else {
                    const char* sel = clawd::notesSelected();
                    if (sel && sel[0] && recorder::playNote(sel)) {
                        // 播成功：刷新列表显示 ▶
                        char names[REC_MAX_NOTES][REC_NOTE_NAME_LEN];
                        uint8_t n = recorder::listNotes(names, REC_MAX_NOTES);
                        // 找当前播放文件在列表中的位置
                        int8_t playingIdx = -1;
                        for (uint8_t i = 0; i < n; i++) {
                            if (strcmp(names[i], sel) == 0) { playingIdx = (int8_t)i; break; }
                        }
                        clawd::showNotes(names, n, playingIdx);
                        clawd::setToast("playing");
                    } else {
                        clawd::setToast("play fail");
                    }
                }
            }
        }
    }

    // ── 键盘笔记本(无审批/问答/会话/帮助/笔记时,k 键开关；笔记本内键盘独占)──
    if (keyEvent && !snapApproval && !snapQuestion && !snapSessions && !snapHelp && !snapNotes) {
        if (snapNotebook) {
            bool save = ks.esc;                            // esc=存盘退出
            bool bs   = ks.backspace;
            for (auto c : ks.word) {
                if (c == '`')  save = true;                // backtick 也存盘退出
                else if (c == '\n' || c == '\r')            // enter/newline
                    clawd::notebookNewline();
                else if (c >= 0x20 && c < 0x7f)            // 可打印 ASCII
                    clawd::notebookKey(c);
            }
            if (bs) clawd::notebookBackspace();
            if (save) {
                const char* txt = clawd::notebookText();
                if (txt && txt[0]) {
                    if (recorder::saveTextNote(txt))
                        clawd::setToast("saved");
                    else
                        clawd::setToast("save fail");
                }
                clawd::hideNotebook(true);
            }
        }
    }

    // ── 快捷 nudge(NORMAL 模式:非审批、非会话、非帮助、非问答、非笔记、非笔记本)──
    // 守卫加 !snapNotes / !snapNotebook：覆盖层打开时键盘归各层独占。
    if (keyEvent && !snapApproval && !snapSessions && !snapHelp && !snapQuestion && !snapNotes && !snapNotebook) {
        // 物理 Enter → 给聚焦的 Claude 终端注入 Enter（语音(v PTT)录完提交用）。
        if (ks.enter) {
            cclink::sendKeyName("enter");
            clawd::setToast("sent: enter");
            sound::play("nudge");
        }
        // 物理 Backspace/Del 键 → 回送退格，纠正语音听写/nudge 打字打错的字符。
        // 用 ks.backspace（HID KEY_BACKSPACE）。upstream inputText.ino 用 status.del，
        // 但真机串口诊断实测 Cardputer-ADV 这颗键置位的是 backspace 而非 del
        // （见 openspec/changes/cardputer-backspace-key/design.md Decisions 更新记录）。
        if (ks.backspace) {
            cclink::sendKeyName("backspace");
            clawd::setToast("sent: backspace");
            sound::play("nudge");
        }
        for (auto c : ks.word) {
            // 音量调节(-/=)与 HELP 切换(h)——本地操作，不发命令
            if (c == '-') { sound::volumeDown(); char t[16]; snprintf(t, sizeof(t), "vol %d", sound::volume()); clawd::setToast(t); break; }
            if (c == '=') { sound::volumeUp();   char t[16]; snprintf(t, sizeof(t), "vol %d", sound::volume()); clawd::setToast(t); break; }
            if (c == 'h' || c == 'H') { clawd::showHelp(); break; }
            // 'n'：本机语音便签录音起停（openspec change cardputer-voice-notes）。本地操作，
            // 不发命令。起录挂载 SD 失败 → toast "no SD" 不进录音态。
            if (c == 'n' || c == 'N') {
                if (recorder::isRecording()) { recorder::endRecord(); clawd::setToast("rec saved"); }
                else if (recorder::beginRecord())      clawd::setToast("rec start");
                else                                   clawd::setToast("no SD");
                break;
            }
            // 'l'：笔记列表浏览+回放（openspec change cardputer-note-playback）。本地操作，
            // 不发命令。列表为空或 SD 未挂载也显示 "(no notes)" 不崩。
            if (c == 'l' || c == 'L') {
                char names[REC_MAX_NOTES][REC_NOTE_NAME_LEN];
                uint8_t n = recorder::listNotes(names, REC_MAX_NOTES);
                int8_t playingIdx = recorder::isPlaying() ? -1 : -1;  // 初始无人在播
                clawd::showNotes(names, n, playingIdx);
                break;
            }
            // 'k'：键盘笔记本（打字 → esc 存 SD）。本地操作，不发命令。
            if (c == 'k' || c == 'K') {
                clawd::showNotebook();
                break;
            }
            for (auto& n : NUDGES) {
                if (c != n.key) continue;
                if (n.keyName) cclink::sendKeyName(n.keyName);
                else { cclink::sendKeyText(n.text); cclink::sendKeyName("enter"); }
                char t[24]; snprintf(t, sizeof(t), "sent: %s", n.label);
                clawd::setToast(t);
                sound::play("nudge");
                break;
            }
        }
    }

    // ── 正常态:角标 + 工具失败 reaction（state 变时）──
    if (cclink::changed()) {
        clawd::setBadge(bs.total, bs.running);
        // 工具出错 → error 临时动画。声音改由 cclink 的 play 字段 wav 负责
        // （hook 事件声音统一走 wav，不再用 tone，避免重复）。
        bool nowFailed = (strncmp(bs.msg, "failed", 6) == 0);
        if (online && nowFailed && !g_wasFailed) clawd::reactError();
        g_wasFailed = nowFailed;
    }

    // ── 多会话轮播 / FIFO 钉：主形象状态来源（openspec cardputer-session-rotation）──
    // 无 per-session 数据 → 回退聚合 deriveAgentState。setState 会重载 GIF，故仅在
    // 目标态变化时调；setSessionTag 便宜（仅存串），每帧刷以更新 [i/N] 与钉态横幅。
    {
        static uint32_t rotNextMs = 0;
        static uint8_t  rotIdx = 0;
        static int      lastAg = -1;
        clawd::setOnline(online);
        uint8_t n = bs.nSessions;
        AgentState target;
        if (!online) {
            target = AgentState::Connecting;               // 未连上：不跑派生/轮播（bs 必为全零）
            clawd::setSessionTag(nullptr, 0, 0, false);
        } else if (n == 0) {
            target = deriveAgentState(bs);                 // 回退：单聚合态
            clawd::setSessionTag(nullptr, 0, 0, false);
        } else {
            int pin = -1; uint32_t best = 0xFFFFFFFFu;     // FIFO 最早等待者（waitSeq>0 最小）
            for (uint8_t i = 0; i < n; i++) {
                uint32_t ws = bs.sessions[i].waitSeq;
                if (ws && ws < best) { best = ws; pin = (int)i; }
            }
            uint8_t cur;
            if (pin >= 0) {
                cur = (uint8_t)pin;                        // 钉：不轮播
                rotNextMs = now + 3000;                    // 解钉后给完整 dwell 再轮
            } else {
                if (rotIdx >= n) rotIdx = 0;
                if (now >= rotNextMs) {                     // 到点切下一个
                    rotIdx = (uint8_t)((rotIdx + 1) % n);
                    bool isIdle = (bs.sessions[rotIdx].state == (int)AgentState::Idle);
                    rotNextMs = now + (isIdle ? 1000u : 3000u);  // 稀释旋钮：idle 短/active 长
                }
                cur = rotIdx;
            }
            target = (AgentState)bs.sessions[cur].state;
            const char* tag = bs.sessions[cur].label[0] ? bs.sessions[cur].label
                                                        : bs.sessions[cur].sid;
            clawd::setSessionTag(tag, cur, n, pin >= 0);
        }
        if ((int)target != lastAg) { clawd::setState(target); lastAg = (int)target; }
    }

    // 体感(覆盖模式下 clawd_player 内部 no-op)
    g_motion.update(dt);
    switch (g_motion.event()) {
        case MotionEvent::Shaken:   clawd::reactDizzy(); break;
        case MotionEvent::PickedUp: clawd::reactHeart(); break;
        default: break;
    }
    // 仅在线且空闲久静才睡；离线改由 Connecting 视觉呈现（openspec cardputer-connecting-state）
    bool idle = (bs.running == 0 && bs.waiting == 0);
    clawd::setSleeping(online && idle && g_motion.stillMs() > STILL_FOR_SLEEP);

    // ── 自动息屏判定（背光层，独立于上面的 sleep.gif）────────────────────
    // 活动 = 物理输入：按键(keyEvent) OR IMU 明显运动（stillMs() 在运动帧自动归零）。
    // g_lastKeyMs 记最后一次按键时刻；IMU 运动直接看 stillMs()，无需单独读 IMU。
    // 唤醒键不消费：keyEvent 只是顺带唤醒背光，按键本身仍照常在上面各层被处理——
    // 更简单，也符合「一次按键既亮屏又生效」的用户预期。
    static uint32_t g_lastKeyMs = 0;
    if (keyEvent) g_lastKeyMs = now;
    // 录音进行中也算活动：屏保持亮，不遮 REC 指示（openspec change cardputer-voice-notes）。
    bool activity = keyEvent || g_motion.stillMs() < SCREEN_OFF_MS || recorder::isRecording()
                    || snapNotes || snapNotebook || recorder::isPlaying();
    if (activity) {
        screenOn();                                  // 息屏态下任一物理活动 → 立即恢复背光（幂等）
    } else if (g_motion.stillMs() > SCREEN_OFF_MS && (now - g_lastKeyMs) > SCREEN_OFF_MS) {
        screenOff();                                 // 既无运动又无按键达阈值 → 熄屏
    }

    // 电量：每 30s 读一次 ADC 电量喂给 NORMAL 顶栏角标；getBatteryLevel() <0=unknown
    // （clawd 侧不显示）。频率对齐 StackChan(CoreS3 每 30s)，避免每帧 ADC 开销。
    // openspec change cardputer-battery-indicator。范本 M5Unified HowToUse.ino:500。
    {
        static uint32_t batNextMs = 0;
        if (now >= batNextMs) {
            batNextMs = now + 30000;
            clawd::setBattery(M5.Power.getBatteryLevel());
        }
    }

    // 录音态：每帧抓一小段写盘（分帧，不阻塞）；驱动持久 REC 指示显示/更新/隐藏。
    if (recorder::isRecording()) recorder::tick();
    clawd::setRecording(recorder::isRecording(), recorder::elapsedMs());

    // 回放态：每帧流式播一小段；播完（EOF）自动回到列表，刷新清除 ▶ 指示。
    static bool g_wasPlaying = false;
    if (recorder::isPlaying()) {
        recorder::tickPlayback();
        g_wasPlaying = true;
    } else if (g_wasPlaying) {
        g_wasPlaying = false;
        // 回放刚结束（EOF 或 stopPlayback）：若列表还开着则刷新，清除 ▶
        if (snapNotes) {
            char names[REC_MAX_NOTES][REC_NOTE_NAME_LEN];
            uint8_t n = recorder::listNotes(names, REC_MAX_NOTES);
            clawd::showNotes(names, n, -1);
        }
    }

    sound::tick();
    clawd::tick(dt);

    static uint32_t hb = 0;
    hb += dt;
    if (hb > 3000) { hb = 0;
        Serial.printf("[main] conn=%d t=%d r=%d w=%d prompt=%s heap=%u\n",
                      online ? 1 : 0, bs.total, bs.running, bs.waiting,
                      bs.promptId[0] ? bs.promptId : "-", (unsigned)ESP.getFreeHeap());
    }
    delay(recorder::isPlaying() ? 1 : 5);  // 回放中缩间隔减少 chunk 间隙
}
