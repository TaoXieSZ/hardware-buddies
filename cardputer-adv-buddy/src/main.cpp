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

static Motion g_motion;
static uint32_t g_lastMs = 0;
static constexpr uint32_t STILL_FOR_SLEEP = 30000;
static constexpr uint32_t APPROVAL_SAFETY_MS = 30000;  // 面板兜底超时(回落 ask)

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

    g_motion.begin();
    clawd::begin();
    sound::begin();
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

    // ── 会话列表(无审批/问题时,tab 开关,esc 关,,/. 选,enter/space 切换)──
    if (!snapApproval && !snapQuestion && keyEvent) {
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
    if (!snapApproval && !snapSessions && keyEvent) {
        if (snapHelp) {
            if (ks.esc) { clawd::hideHelp(); }
            for (auto c : ks.word) {
                if (c == 'h' || c == 'H' || c == '`') { clawd::hideHelp(); break; }
            }
        }
    }

    // ── 快捷 nudge(NORMAL 模式:非审批、非会话、非帮助、非问答)──
    // 守卫加 !snapQuestion：问答面板开着时键盘归问答层独占（与审批/会话/帮助一致），
    // 否则按 'c'(chat) 会连带触发 NORMAL 层 'c'(commit) 双触发。
    if (keyEvent && !snapApproval && !snapSessions && !snapHelp && !snapQuestion) {
        for (auto c : ks.word) {
            // 音量调节(-/=)与 HELP 切换(h)——本地操作，不发命令
            if (c == '-') { sound::volumeDown(); char t[16]; snprintf(t, sizeof(t), "vol %d", sound::volume()); clawd::setToast(t); break; }
            if (c == '=') { sound::volumeUp();   char t[16]; snprintf(t, sizeof(t), "vol %d", sound::volume()); clawd::setToast(t); break; }
            if (c == 'h' || c == 'H') { clawd::showHelp(); break; }
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

    // ── 正常态:真实状态驱动 clawd + 角标 ──
    if (cclink::changed()) {
        clawd::setBadge(bs.total, bs.running);
        clawd::setState(deriveAgentState(bs));
        // 工具出错 → error 临时动画。声音改由 cclink 的 play 字段 wav 负责
        // （hook 事件声音统一走 wav，不再用 tone，避免重复）。
        bool nowFailed = (strncmp(bs.msg, "failed", 6) == 0);
        if (online && nowFailed && !g_wasFailed) clawd::reactError();
        g_wasFailed = nowFailed;
    }

    // 体感(覆盖模式下 clawd_player 内部 no-op)
    g_motion.update(dt);
    switch (g_motion.event()) {
        case MotionEvent::Shaken:   clawd::reactDizzy(); break;
        case MotionEvent::PickedUp: clawd::reactHeart(); break;
        default: break;
    }
    // 离线 或 (空闲且久静) → clawd 睡眠
    bool idle = (bs.running == 0 && bs.waiting == 0);
    clawd::setSleeping(!online || (idle && g_motion.stillMs() > STILL_FOR_SLEEP));

    sound::tick();
    clawd::tick(dt);

    static uint32_t hb = 0;
    hb += dt;
    if (hb > 3000) { hb = 0;
        Serial.printf("[main] conn=%d t=%d r=%d w=%d prompt=%s heap=%u\n",
                      online ? 1 : 0, bs.total, bs.running, bs.waiting,
                      bs.promptId[0] ? bs.promptId : "-", (unsigned)ESP.getFreeHeap());
    }
    delay(5);
}
