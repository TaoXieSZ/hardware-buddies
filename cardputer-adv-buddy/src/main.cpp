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
    // v = PTT：inject Space → 配合 Claude Code `/voice tap` 模式切换录音
    {'v', nullptr,                "space",   "ptt"},
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
    bool hasPrompt = bs.promptId[0] != 0;
    if (hasPrompt && strcmp(bs.promptId, g_shownId) != 0) {       // 新审批
        strncpy(g_shownId, bs.promptId, sizeof(g_shownId) - 1);
        g_shownId[sizeof(g_shownId) - 1] = 0;
        g_promptShownMs = now;
        clawd::showApproval(bs.promptTool, bs.promptHint);
    }
    if (snapApproval) {
        if (keyEvent) {
            const char* dec = nullptr;
            // 注意: Cardputer 的 "esc" 键单按产出 backtick '`'（KEY_ESCAPE 在 fn 层），
            // 故 deny 同时接受 '`' / fn+esc(ks.esc) / 'n'。
            if (ks.enter)     dec = "once";
            else if (ks.esc)  dec = "deny";
            else for (auto c : ks.word) {
                if (c == ' ')                         { dec = "once";   break; }  // space 也 ok
                if (c == '`' || c == 'n' || c == 'N') { dec = "deny";   break; }
                if (c == 'a' || c == 'A')             { dec = "always"; break; }
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
            bool submit = ks.enter;
            bool cancel = ks.esc;            // fn+esc
            for (auto c : ks.word) {
                if (c == '`')                  cancel = true;   // 单按 esc 键 = backtick
                else if (c == ' ')             submit = true;
                else if (c == ',' || c == ';') clawd::questionMove(-1);
                else if (c == '.' || c == '/') clawd::questionMove(1);
                else if (c >= '1' && c <= '9') {
                    clawd::questionJumpTo(c - '1');
                    if (clawd::questionMulti()) clawd::questionToggle();  // 多选: toggle 勾选
                    else submit = true;                                  // 单选: 即选即交
                }
            }
            if (submit || cancel) {
                if (submit) {
                    const char* ids[6];
                    uint8_t nid = clawd::questionSelectedIds(ids, 6);
                    if (nid > 0) {
                        cclink::sendAnswerQuestion(clawd::questionRid(), ids, nid);
                        clawd::setToast("answered");
                        sound::play("nudge");
                    }
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

    // ── 快捷 nudge(NORMAL 模式:非审批、非会话、非帮助)──
    if (keyEvent && !snapApproval && !snapSessions && !snapHelp) {
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
