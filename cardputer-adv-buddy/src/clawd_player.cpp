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
enum Mode { NORMAL, APPROVAL, SESSIONS, HELP };
Mode mode_ = NORMAL;

// NORMAL 角标 + toast
int badgeTotal_ = 0, badgeRunning_ = 0;
char toast_[24] = {0};
int32_t toastMs_ = 0;

// APPROVAL
char apTool_[40] = {0}, apHint_[92] = {0};

// SESSIONS
char sess_[8][92] = {{0}};
uint8_t sessN_ = 0;
int sessScroll_ = 0;
int sessTotal_ = 0;  // 真实 session 数（含 subagent 被过滤的）

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
        case AgentState::ToolUse:      return "busy_1.gif";
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
    canvas.setTextColor(0x07E0, BG); canvas.drawString("[spc/ok]yes", 6, canvasH - 14);
    canvas.setTextColor(0xF800, BG); canvas.drawString("[`]no", 90, canvasH - 14);
    canvas.setTextColor(0x8410, BG); canvas.drawString("[a]always", 138, canvasH - 14);
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
    canvas.drawString("v=ptt       h=help", L, y); y += rowH + 2;

    canvas.setTextColor(0x8410, BG);
    canvas.drawString("APPROVE: ok  esc/n=no  a=always", L, y); y += rowH;
    canvas.setTextColor(0x8410, BG);
    canvas.drawString("SESSION: tab  ,/.=scroll", L, y);
}

// 会话列表（SESSIONS，只读）
void drawSessions() {
    canvas.fillSprite(BG);
    canvas.setTextColor(CLAWD, BG);
    canvas.setTextSize(1);
    canvas.setTextDatum(top_left);
    char title[24];
    snprintf(title, sizeof(title), "SESSIONS (%d)", sessTotal_);
    canvas.drawString(title, 6, 2);
    canvas.setTextColor(TFT_WHITE, BG);
    const int rowH = 12, top = 18, rows = (canvasH - top) / rowH;  // ~9 行
    if (sessN_ == 0) {
        canvas.setTextColor(0x8410, BG);
        canvas.drawString("(subagent work in progress)", 6, top);
    } else {
        for (int r = 0; r < rows; r++) {
            int idx = sessScroll_ + r;
            if (idx >= sessN_) break;
            char row[40];
            strncpy(row, sess_[idx], sizeof(row) - 1); row[sizeof(row) - 1] = 0;
            canvas.drawString(row, 6, top + r * rowH);
        }
    }
    if (sessScroll_ > 0) canvas.drawString("^", canvasW - 10, top);
    if (sessScroll_ + rows < sessN_) canvas.drawString("v", canvasW - 10, canvasH - rowH);
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

void showSessions(const char lines[][92], uint8_t n, int total) {
    sessN_ = n > 8 ? 8 : n;
    for (uint8_t i = 0; i < sessN_; i++) { strncpy(sess_[i], lines[i], 91); sess_[i][91] = 0; }
    sessScroll_ = 0;
    sessTotal_ = total;
    if (mode_ != APPROVAL) mode_ = SESSIONS;
}
void hideSessions() { if (mode_ == SESSIONS) { mode_ = NORMAL; strcpy(curFile, ""); applyTarget(); } }
void sessionsScroll(int delta) {
    if (mode_ != SESSIONS) return;
    sessScroll_ += delta;
    if (sessScroll_ < 0) sessScroll_ = 0;
    if (sessScroll_ >= sessN_) sessScroll_ = sessN_ > 0 ? sessN_ - 1 : 0;
}
bool sessionsVisible() { return mode_ == SESSIONS; }

void showHelp() { if (mode_ != APPROVAL && mode_ != SESSIONS) mode_ = HELP; }
void hideHelp() { if (mode_ == HELP) { mode_ = NORMAL; strcpy(curFile, ""); applyTarget(); } }
bool helpVisible() { return mode_ == HELP; }

void tick(uint32_t dtMs) {
    if (!ready) return;

    if (mode_ == APPROVAL) { drawApproval(); canvas.pushSprite(0, 0); return; }
    if (mode_ == SESSIONS) { drawSessions(); canvas.pushSprite(0, 0); return; }
    if (mode_ == HELP)     { drawHelp();     canvas.pushSprite(0, 0); return; }

    // NORMAL：推进 GIF + 角标
    if (reactionMs_ > 0) {
        reactionMs_ -= (int32_t)dtMs;
        if (reactionMs_ <= 0) { reactionFile_ = nullptr; applyTarget(); }
    }
    if (!gifOpen) return;
    uint32_t now = millis();
    if (now < nextFrameAt) return;
    int delayMs = 0;
    if (!gif.playFrame(false, &delayMs)) { gif.reset(); gif.playFrame(false, &delayMs); }
    drawBadge();
    if (toastMs_ > 0) { toastMs_ -= (int32_t)dtMs; drawToast(); }
    canvas.pushSprite(0, 0);
    nextFrameAt = now + (delayMs > 0 ? delayMs : 100);
}

}  // namespace clawd
