// AnimatedGIF + LittleFS 文件回调与 GIFDRAW 逐行绘制逐字参照 buddy 家族成熟实现：
//   ../claude-code-buddy/src/character.cpp。本实现自带一块 240x135 sprite 作合成器，
//   按模式（NORMAL/APPROVAL/SESSIONS）合成后 push。
#include "clawd_player.h"
#include "M5Cardputer.h"
#include <LittleFS.h>
#include <AnimatedGIF.h>
#include <string.h>

namespace {
AnimatedGIF gif;
File gifFile;
M5Canvas canvas(&M5Cardputer.Display);

bool fsOk = false, gifOpen = false, ready = false;
int canvasW = 240, canvasH = 135;
int gifX = 0, gifY = 0, gifW = 0, gifH = 0;
const uint16_t BG = 0x0000;          // clawd manifest bg=#000000
const uint16_t CLAWD = 0xDBAA;       // clawd body #D97757 → RGB565
uint32_t nextFrameAt = 0;
char curFile[24] = {0};

AgentState baseState_ = AgentState::Idle;
bool sleeping_ = false;
int32_t reactionMs_ = 0;
const char* reactionFile_ = nullptr;

// 合成模式：优先级 APPROVAL > SESSIONS > HELP > NORMAL
enum Mode { NORMAL, APPROVAL, QUESTION, SESSIONS, HELP };
Mode mode_ = NORMAL;

// NORMAL 角标 + toast
int badgeTotal_ = 0, badgeRunning_ = 0;
char toast_[24] = {0};
int32_t toastMs_ = 0;
// 多会话轮播：当前会话标识 + 轮播位置 + 钉态（rotation）
char rotTag_[40] = {0};
int  rotIdx_ = 0, rotTotal_ = 0;
bool rotPinned_ = false;
// working(ToolUse) 态多动作：在 busy_0..3 间轮换，别老敲键盘
int      busyVariant_ = 0;
uint32_t busyNextMs_ = 0;

// APPROVAL
char apTool_[40] = {0}, apHint_[92] = {0};

// SESSIONS（per-session 可选中列表）
SessionInfo sess_[16];
uint8_t sessN_ = 0;
int sessSel_ = 0;    // 选中索引（高亮 + enter 切换的目标）
int sessScroll_ = 0; // viewport 顶部索引（跟随选中）
int sessTotal_ = 0;  // 真实 session 数（payload total）

// QUESTION（AskUserQuestion 应答器）— showQuestion 时的快照
struct _QOpt { char id[8] = {0}; char label[40] = {0}; };
char qRid_[100] = {0};
char qHeader_[24] = {0};
char qText_[92] = {0};
bool qMulti_ = false;
_QOpt qOpts_[6];
uint8_t qN_ = 0;
int qSel_ = 0;             // 光标
bool qChecked_[6] = {false}; // multiSelect 勾选态

// --- LittleFS 文件回调（照搬 character.cpp）---
void* openCb(const char* fn, int32_t* pSize) {
    gifFile = LittleFS.open(fn, "r");
    if (!gifFile) return nullptr;
    *pSize = gifFile.size();
    return (void*)&gifFile;
}
void closeCb(void* h) { File* f = (File*)h; if (f) f->close(); }
int32_t readCb(GIFFILE* pf, uint8_t* buf, int32_t len) {
    File* f = (File*)pf->fHandle;
    int32_t n = f->read(buf, len);
    pf->iPos = f->position();
    return n;
}
int32_t seekCb(GIFFILE* pf, int32_t pos) {
    File* f = (File*)pf->fHandle;
    f->seek(pos);
    pf->iPos = (int32_t)f->position();
    return pf->iPos;
}
void drawCb(GIFDRAW* d) {
    uint16_t* pal = d->pPalette;
    uint8_t* src = d->pPixels;
    uint8_t t = d->ucTransparent;
    bool hasT = d->ucHasTransparency;
    int y = gifY + d->iY + d->y;
    if (y < 0 || y >= canvasH) return;
    int x0 = gifX + d->iX;
    int w = d->iWidth;
    if (x0 < 0) { src -= x0; w += x0; x0 = 0; }
    if (x0 + w > canvasW) w = canvasW - x0;
    if (w <= 0) return;
    for (int i = 0; i < w; i++) {
        uint8_t idx = src[i];
        canvas.drawPixel(x0 + i, y, (hasT && idx == t) ? BG : pal[idx]);
    }
}

const char* fileForState(AgentState s) {
    switch (s) {
        case AgentState::Idle:         return "idle.gif";
        case AgentState::Thinking:     return "clawd-thinking.gif";
        case AgentState::ToolUse: {     // working：busy_0..3 轮换（见 tick busy 计时器）
            static const char* kBusy[4] = {"busy_0.gif", "busy_1.gif", "busy_2.gif", "busy_3.gif"};
            return kBusy[busyVariant_ & 3];
        }
        case AgentState::Approval:     return "attention.gif";
        case AgentState::Done:         return "celebrate.gif";
        case AgentState::Notification: return "clawd-notification.gif";
        default:                       return "idle.gif";
    }
}
const char* targetFile() {
    if (reactionFile_) return reactionFile_;
    if (sleeping_)     return "sleep.gif";
    return fileForState(baseState_);
}
void openFile(const char* fn) {
    if (gifOpen) { gif.close(); gifOpen = false; }
    char full[48];
    snprintf(full, sizeof(full), "/characters/clawd/%s", fn);
    if (gif.open(full, openCb, closeCb, readCb, seekCb, drawCb)) {
        gifOpen = true;
        gifW = gif.getCanvasWidth();
        gifH = gif.getCanvasHeight();
        gifX = (canvasW - gifW) / 2;
        gifY = (canvasH - gifH) / 2;
        if (gifX < 0) gifX = 0;
        if (gifY < 0) gifY = 0;
        canvas.fillSprite(BG);
        nextFrameAt = 0;
        strncpy(curFile, fn, sizeof(curFile) - 1);
    } else {
        Serial.printf("[clawd] open fail %s err=%d\n", full, gif.getLastError());
    }
}
void applyTarget() {
    const char* want = targetFile();
    if (strcmp(want, curFile) != 0) openFile(want);
}

// 右上角会话计数角标（NORMAL）
void drawBadge() {
    char b[16];
    snprintf(b, sizeof(b), "%d/%d", badgeTotal_, badgeRunning_);  // T/R 总会话/运行中
    canvas.setTextSize(1);
    int w = (int)strlen(b) * 6 + 6;
    canvas.fillRect(canvasW - w, 0, w, 12, BG);
    canvas.setTextColor(CLAWD, BG);
    canvas.setTextDatum(top_right);
    canvas.drawString(b, canvasW - 2, 2);
    canvas.setTextDatum(top_left);
}

// 多会话轮播：顶栏左会话标识 + [i/N]；钉态底部横幅（NORMAL）。openspec cardputer-session-rotation。
void drawSessionTag() {
    if (rotTotal_ <= 0 || !rotTag_[0]) return;
    canvas.setFont(&fonts::efontCN_12);   // label 可能是中文 cmux 名
    canvas.setTextSize(1);
    canvas.setTextDatum(top_left);
    char line[52];
    snprintf(line, sizeof(line), "%s [%d/%d]", rotTag_, rotIdx_ + 1, rotTotal_);
    canvas.fillRect(0, 0, canvasW - 44, 13, BG);   // 清左侧条，留右上 badge(~40px)
    canvas.setTextColor(0x8410, BG);
    canvas.drawString(line, 2, 1);
    if (rotPinned_) {                              // 钉态：底部橙横幅（与审批同色系）
        canvas.fillRect(0, canvasH - 14, canvasW, 14, 0xFB00);
        canvas.setTextColor(TFT_WHITE, 0xFB00);
        canvas.setTextDatum(bottom_left);
        char b[52];
        snprintf(b, sizeof(b), "input: %s", rotTag_);
        canvas.drawString(b, 4, canvasH - 2);
    }
    canvas.setFont(&fonts::Font0);
    canvas.setTextDatum(top_left);
}

// 底部短暂 toast（nudge 发送反馈）
void drawToast() {
    canvas.fillRect(0, canvasH - 14, canvasW, 14, BG);
    canvas.setTextColor(0x07E0, BG);   // 绿
    canvas.setTextSize(1);
    canvas.setTextDatum(bottom_center);
    canvas.drawString(toast_, canvasW / 2, canvasH - 2);
    canvas.setTextDatum(top_left);
}

// 审批面板（APPROVAL）
void drawApproval() {
    canvas.fillSprite(BG);
    canvas.setTextColor(TFT_WHITE, BG);
    canvas.setTextDatum(top_left);
    // 顶条
    canvas.fillRect(0, 0, canvasW, 16, 0xFB00);  // 橙
    canvas.setTextColor(TFT_BLACK, 0xFB00);
    canvas.setTextSize(1);
    canvas.drawString("APPROVE?", 6, 4);
    // 工具名（大）
    canvas.setTextColor(0xFD20, BG);
    canvas.setTextSize(2);
    canvas.drawString(apTool_, 6, 26);
    // 参数（小，截断）
    canvas.setTextColor(TFT_WHITE, BG);
    canvas.setTextSize(1);
    char hint[40];
    strncpy(hint, apHint_, sizeof(hint) - 1); hint[sizeof(hint) - 1] = 0;
    canvas.drawString(hint, 6, 54);
    // 按键提示
    canvas.setTextColor(0x07E0, BG); canvas.drawString("spc=yes", 6, canvasH - 14);
    canvas.setTextColor(0xF800, BG); canvas.drawString("`=no", 84, canvasH - 14);
    canvas.setTextColor(0x8410, BG); canvas.drawString("ctrl=always", 128, canvasH - 14);
}

// 键位说明（HELP）
void drawHelp() {
    canvas.fillSprite(BG);
    canvas.setTextDatum(top_left);
    canvas.setTextSize(1);
    canvas.setTextColor(CLAWD, BG);
    canvas.drawString("KEY MAP", 6, 2);
    canvas.drawFastHLine(0, 12, canvasW, CLAWD);

    const int L = 6, rowH = 11;
    int y = 15;
    canvas.setTextColor(0x8410, BG);   // 灰
    canvas.drawString("NUDGE:", L, y); y += rowH;
    canvas.setTextColor(TFT_WHITE, BG);
    canvas.drawString("1=continue  2=run tests", L, y); y += rowH;
    canvas.drawString("3=explain   4=stop(esc)", L, y); y += rowH;
    canvas.drawString("5=yes       r=retry", L, y); y += rowH;
    canvas.drawString("c=commit    f=fix this", L, y); y += rowH;
    canvas.drawString("v=ptt  h=help  -/=vol", L, y); y += rowH + 2;

    canvas.setTextColor(0x8410, BG);
    canvas.drawString("APPROVE: spc=y `=n ctrl=alw", L, y); y += rowH;
    canvas.setTextColor(0x8410, BG);
    canvas.drawString("SESS: tab ,/.=sel enter=go", L, y);
}

// 会话列表（SESSIONS，per-session 可选中）。选中项高亮，enter 回送 selectSession。
void drawSessions() {
    canvas.fillSprite(BG);
    canvas.setTextColor(CLAWD, BG);
    canvas.setTextSize(1);
    canvas.setFont(&fonts::efontCN_12);   // 中文会话名渲染（默认字体无 CJK glyph）
    canvas.setTextDatum(top_left);
    char title[24];
    snprintf(title, sizeof(title), "SESSIONS (%d)", sessTotal_);
    canvas.drawString(title, 6, 2);
    const int rowH = 14, top = 16, rows = (canvasH - top) / rowH;  // efontCN_12 行高
    if (sessN_ == 0) {
        canvas.setTextColor(0x8410, BG);
        canvas.drawString("(no sessions)", 6, top);
    } else {
        for (int r = 0; r < rows; r++) {
            int idx = sessScroll_ + r;
            if (idx >= sessN_) break;
            bool sel = (idx == sessSel_);
            int y = top + r * rowH;
            if (sel) canvas.fillRect(0, y - 1, canvasW, rowH, 0x2945);  // 选中行高亮底
            // agent 标记：claude=黄 "cc"，cursor=灰蓝 "cu"，codex=绿 "cx"。颜色+文字双重区分。
            const char* agent = sess_[idx].agent;
            const char* atag; uint16_t rowCol;
            if (strcmp(agent, "cursor") == 0)      { atag = "cu"; rowCol = 0xCE59; }
            else if (strcmp(agent, "codex") == 0)  { atag = "cx"; rowCol = 0x07E5; }
            else                                   { atag = "cc"; rowCol = 0xFD20; }
            canvas.setTextColor(sel ? TFT_WHITE : rowCol, sel ? 0x2945 : BG);
            // 名字优先 cmux auto-name（label）；没有时 fallback sid 前 8 字符。
            char sid8[9];
            strncpy(sid8, sess_[idx].sid, 8); sid8[8] = 0;
            const char* nm = sess_[idx].label[0] ? sess_[idx].label : sid8;
            char nm2[40]; utf8lcpy(nm2, nm, sizeof(nm2));  // UTF-8 安全；超宽由 sprite 裁剪
            char row[72];
            snprintf(row, sizeof(row), "%c%d %s %s", sel ? '>' : ' ', idx + 1, atag, nm2);
            canvas.drawString(row, 6, y);
            canvas.drawString(sess_[idx].running ? "run" : "idle", canvasW - 32, y);
        }
    }
    if (sessScroll_ > 0) canvas.drawString("^", canvasW - 10, top);
    if (sessScroll_ + rows < sessN_) canvas.drawString("v", canvasW - 10, canvasH - rowH);
    canvas.setFont(&fonts::Font0);   // 复位默认字体，避免污染其他面板
}

// AskUserQuestion 应答面板：header + 问题 + N 选项（高亮当前；multiSelect 显勾选）
void drawQuestion() {
    canvas.fillSprite(BG);
    canvas.setTextColor(CLAWD, BG);
    canvas.setTextSize(1);
    canvas.setFont(&fonts::efontCN_12);   // 中文 header/选项/提示渲染（默认字体无 CJK glyph）
    canvas.setTextDatum(top_left);
    char title[28];
    snprintf(title, sizeof(title), "? %s", qHeader_[0] ? qHeader_ : "Question");
    canvas.drawString(title, 6, 1);
    canvas.setTextColor(0xCE59, BG);
    char qt[92]; utf8lcpy(qt, qText_, sizeof(qt));  // 单行，超宽由 sprite 裁剪
    canvas.drawString(qt, 6, 15);
    const int rowH = 14, top = 30, rows = (canvasH - top - 13) / rowH;  // efontCN_12 行高
    for (int r = 0; r < rows; r++) {
        int idx = r;                 // 选项 ≤6，单屏无需滚动
        if (idx >= qN_) break;
        bool sel = (idx == qSel_);
        int y = top + r * rowH;
        if (sel) canvas.fillRect(0, y - 1, canvasW, rowH, 0x2945);
        canvas.setTextColor(sel ? TFT_WHITE : 0xCE59, sel ? 0x2945 : BG);
        char lbl[40]; utf8lcpy(lbl, qOpts_[idx].label, sizeof(lbl));  // UTF-8 安全；超宽 sprite 裁剪
        char row[64];
        if (qMulti_)
            snprintf(row, sizeof(row), "%c[%c]%d %s", sel ? '>' : ' ',
                     qChecked_[idx] ? 'x' : ' ', idx + 1, lbl);
        else
            snprintf(row, sizeof(row), "%c %d %s", sel ? '>' : ' ', idx + 1, lbl);
        canvas.drawString(row, 6, y);
    }
    canvas.setTextColor(0x8410, BG);
    canvas.drawString(qMulti_ ? "1-N tog·ok交·c聊·esc跳" : "1-N 选·ok·c聊·esc跳", 6, canvasH - 13);
    canvas.setFont(&fonts::Font0);   // 复位默认字体
}
}  // namespace

namespace clawd {

void begin() {
    canvasW = M5Cardputer.Display.width();
    canvasH = M5Cardputer.Display.height();
    fsOk = LittleFS.begin(false);
    if (!fsOk) Serial.println("[clawd] LittleFS mount FAIL");
    canvas.createSprite(canvasW, canvasH);
    canvas.fillSprite(BG);
    canvas.pushSprite(0, 0);
    gif.begin(LITTLE_ENDIAN_PIXELS);
    ready = fsOk;
    if (ready) applyTarget();
}

bool ok() { return ready; }

void setState(AgentState s) { baseState_ = s; if (ready && mode_ == NORMAL) applyTarget(); }
void setBadge(int total, int running) { badgeTotal_ = total; badgeRunning_ = running; }
void setSessionTag(const char* tag, int idx, int total, bool pinned) {
    if (tag) utf8lcpy(rotTag_, tag, sizeof(rotTag_)); else rotTag_[0] = 0;
    rotIdx_ = idx; rotTotal_ = total; rotPinned_ = pinned;
}
void setToast(const char* text) {
    strncpy(toast_, text ? text : "", sizeof(toast_) - 1);
    toast_[sizeof(toast_) - 1] = 0;
    toastMs_ = 1500;
}

void setSleeping(bool sleep) {
    if (sleep == sleeping_) return;
    sleeping_ = sleep;
    if (ready && mode_ == NORMAL) applyTarget();
}
void reactHeart() { reactionFile_ = "heart.gif"; reactionMs_ = 1500; if (ready && mode_ == NORMAL) applyTarget(); }
void reactDizzy() { reactionFile_ = "dizzy.gif"; reactionMs_ = 1200; if (ready && mode_ == NORMAL) applyTarget(); }
// 工具出错：error 用 reaction 而非持久状态——"failed:" 后紧跟 done/ready 会一闪而过，
// reaction 临时覆盖 2.5s 保证 error 动画显示足够时长。
void reactError() { reactionFile_ = "error-120.gif"; reactionMs_ = 2500; if (ready && mode_ == NORMAL) applyTarget(); }

void showApproval(const char* tool, const char* hint) {
    strncpy(apTool_, tool ? tool : "", sizeof(apTool_) - 1); apTool_[sizeof(apTool_) - 1] = 0;
    strncpy(apHint_, hint ? hint : "", sizeof(apHint_) - 1); apHint_[sizeof(apHint_) - 1] = 0;
    mode_ = APPROVAL;
}
void hideApproval() { if (mode_ == APPROVAL) { mode_ = NORMAL; strcpy(curFile, ""); applyTarget(); } }
bool approvalVisible() { return mode_ == APPROVAL; }

void showSessions(const BuddyState& bs) {
    sessN_ = bs.nSessions > 16 ? 16 : bs.nSessions;
    for (uint8_t i = 0; i < sessN_; i++) {
        strncpy(sess_[i].sid, bs.sessions[i].sid, sizeof(sess_[i].sid) - 1);
        sess_[i].sid[sizeof(sess_[i].sid) - 1] = 0;
        sess_[i].running = bs.sessions[i].running;
        strncpy(sess_[i].label, bs.sessions[i].label, sizeof(sess_[i].label) - 1);
        sess_[i].label[sizeof(sess_[i].label) - 1] = 0;
        strncpy(sess_[i].agent, bs.sessions[i].agent, sizeof(sess_[i].agent) - 1);
        sess_[i].agent[sizeof(sess_[i].agent) - 1] = 0;
    }
    sessScroll_ = 0;
    sessSel_ = 0;
    sessTotal_ = bs.total;
    if (mode_ != APPROVAL && mode_ != QUESTION) mode_ = SESSIONS;
}
void hideSessions() { if (mode_ == SESSIONS) { mode_ = NORMAL; strcpy(curFile, ""); applyTarget(); } }
void sessionsMove(int delta) {
    if (mode_ != SESSIONS || sessN_ == 0) return;
    sessSel_ += delta;
    if (sessSel_ < 0) sessSel_ = 0;
    if (sessSel_ >= sessN_) sessSel_ = sessN_ - 1;
    // viewport 跟随选中：选中项移出可视区时滚动。
    const int rowH = 12, top = 18, rows = (canvasH - top) / rowH;
    if (sessSel_ < sessScroll_) sessScroll_ = sessSel_;
    else if (sessSel_ >= sessScroll_ + rows) sessScroll_ = sessSel_ - rows + 1;
}
const char* sessionsSelectedSid() {
    if (mode_ != SESSIONS || sessSel_ < 0 || sessSel_ >= sessN_) return "";
    return sess_[sessSel_].sid;
}
bool sessionsVisible() { return mode_ == SESSIONS; }

void showQuestion(const BuddyState& bs) {
    qN_ = bs.question.nOptions > 6 ? 6 : bs.question.nOptions;
    strncpy(qRid_, bs.question.rid, sizeof(qRid_) - 1); qRid_[sizeof(qRid_) - 1] = 0;
    strncpy(qHeader_, bs.question.header, sizeof(qHeader_) - 1); qHeader_[sizeof(qHeader_) - 1] = 0;
    strncpy(qText_, bs.question.text, sizeof(qText_) - 1); qText_[sizeof(qText_) - 1] = 0;
    qMulti_ = bs.question.multi;
    for (uint8_t i = 0; i < qN_; i++) {
        strncpy(qOpts_[i].id, bs.question.options[i].id, sizeof(qOpts_[i].id) - 1);
        qOpts_[i].id[sizeof(qOpts_[i].id) - 1] = 0;
        strncpy(qOpts_[i].label, bs.question.options[i].label, sizeof(qOpts_[i].label) - 1);
        qOpts_[i].label[sizeof(qOpts_[i].label) - 1] = 0;
        qChecked_[i] = false;
    }
    qSel_ = 0;
    mode_ = QUESTION;   // 仅次于 APPROVAL（见 showSessions/showHelp 的避让）
}
void hideQuestion() { if (mode_ == QUESTION) { mode_ = NORMAL; strcpy(curFile, ""); applyTarget(); } }
void questionMove(int delta) {
    if (mode_ != QUESTION || qN_ == 0) return;
    qSel_ += delta;
    if (qSel_ < 0) qSel_ = 0;
    if (qSel_ >= qN_) qSel_ = qN_ - 1;
}
void questionToggle() {
    if (mode_ == QUESTION && qMulti_ && qSel_ >= 0 && qSel_ < qN_) qChecked_[qSel_] = !qChecked_[qSel_];
}
void questionJumpTo(int idx) { if (mode_ == QUESTION && idx >= 0 && idx < qN_) qSel_ = idx; }
bool questionMulti() { return mode_ == QUESTION && qMulti_; }
const char* questionRid() { return mode_ == QUESTION ? qRid_ : ""; }
uint8_t questionSelectedIds(const char** out, uint8_t maxN) {
    if (mode_ != QUESTION) return 0;
    uint8_t n = 0;
    if (qMulti_) {
        for (uint8_t i = 0; i < qN_ && n < maxN; i++) if (qChecked_[i]) out[n++] = qOpts_[i].id;
    } else if (qSel_ >= 0 && qSel_ < qN_ && maxN > 0) {
        out[n++] = qOpts_[qSel_].id;
    }
    return n;
}
bool questionVisible() { return mode_ == QUESTION; }

void showHelp() { if (mode_ != APPROVAL && mode_ != QUESTION && mode_ != SESSIONS) mode_ = HELP; }
void hideHelp() { if (mode_ == HELP) { mode_ = NORMAL; strcpy(curFile, ""); applyTarget(); } }
bool helpVisible() { return mode_ == HELP; }

void tick(uint32_t dtMs) {
    if (!ready) return;

    if (mode_ == APPROVAL) { drawApproval(); canvas.pushSprite(0, 0); return; }
    if (mode_ == QUESTION) { drawQuestion(); canvas.pushSprite(0, 0); return; }
    if (mode_ == SESSIONS) { drawSessions(); canvas.pushSprite(0, 0); return; }
    if (mode_ == HELP)     { drawHelp();     canvas.pushSprite(0, 0); return; }

    // NORMAL：推进 GIF + 角标
    if (reactionMs_ > 0) {
        reactionMs_ -= (int32_t)dtMs;
        if (reactionMs_ <= 0) { reactionFile_ = nullptr; applyTarget(); }
    }
    if (!gifOpen) return;
    uint32_t now = millis();
    // working(ToolUse) 态：每 ~2.5s 换一个 busy 变体（busy_0..3），别老敲键盘。
    // 仅在纯 working（无 reaction/睡眠）时换；applyTarget 重开新变体 GIF。
    static bool prevBusy = false;
    bool nowBusy = (baseState_ == AgentState::ToolUse && !sleeping_ && reactionMs_ <= 0);
    if (nowBusy) {
        if (!prevBusy) busyNextMs_ = now + 2500;        // 刚进 working：当前变体先放一会儿
        else if (now >= busyNextMs_) {
            busyVariant_ = (busyVariant_ + 1) & 3;
            busyNextMs_ = now + 2500;
            applyTarget();                              // 切到新 busy 变体
        }
    }
    prevBusy = nowBusy;
    if (now < nextFrameAt) return;
    int delayMs = 0;
    if (!gif.playFrame(false, &delayMs)) { gif.reset(); gif.playFrame(false, &delayMs); }
    drawBadge();
    if (toastMs_ > 0) { toastMs_ -= (int32_t)dtMs; drawToast(); }
    drawSessionTag();   // 顶栏会话标识 + 钉态横幅（盖在 toast 之上）
    canvas.pushSprite(0, 0);
    nextFrameAt = now + (delayMs > 0 ? delayMs : 100);
}

}  // namespace clawd
