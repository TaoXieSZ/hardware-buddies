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
bool online_ = false;   // BLE 是否连上 cc-bridge；!online_ 时顶栏常驻 Connecting...
int32_t reactionMs_ = 0;
const char* reactionFile_ = nullptr;

// 合成模式：优先级 APPROVAL > QUESTION > SESSIONS > NOTES > NOTEBOOK > HELP > NORMAL
enum Mode { NORMAL, APPROVAL, QUESTION, SESSIONS, NOTES, NOTEBOOK, HELP };
Mode mode_ = NORMAL;

// NORMAL 角标 + toast
int badgeTotal_ = 0, badgeRunning_ = 0;
int8_t batPct_ = -1;   // 电量 %（<0=unknown 不显示）。openspec cardputer-battery-indicator
char toast_[24] = {0};
int32_t toastMs_ = 0;
// 本机录音持久指示（顶栏左 "REC mm:ss" 红）。录音是持续态，非 1.5s toast。
// openspec change cardputer-voice-notes。
bool     recording_ = false;
uint32_t recElapsedMs_ = 0;
// 多会话轮播：当前会话标识 + 轮播位置 + 钉态（rotation）
char rotTag_[40] = {0};
int  rotIdx_ = 0, rotTotal_ = 0;
bool rotPinned_ = false;
// working(ToolUse) 态多动作：在 busy_0..3 间轮换，别老敲键盘
int      busyVariant_ = 0;
uint32_t busyNextMs_ = 0;
// Idle 态偶尔飘一下 idle-reading（低频随机，非均匀轮播）。openspec cardputer-idle-variety。
int      idleVariant_ = 0;   // 0=idle.gif / 1=clawd-idle-reading.gif
uint32_t idleNextMs_ = 0;

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

// NOTES（笔记浏览+回放）— showNotes 时快照，复用 SESSIONS 滚动列表范式
char notesNames_[32][32];
uint8_t notesN_ = 0;
int notesSel_ = 0;
int notesScroll_ = 0;       // viewport 顶部索引
int8_t notesPlaying_ = -1;  // 正在回放的笔记索引（-1=无），用于列表显示 ▶

// NOTEBOOK（键盘笔记本）— 简易文本编辑器
#define NB_BUF_SIZE 600
char nbBuf_[NB_BUF_SIZE] = {0};
int  nbLen_ = 0, nbCursor_ = 0;     // 文本长度 + 光标位置（字符索引）

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
        case AgentState::Idle:          // idle.gif 为主，偶尔飘一下 idle-reading（见 tick idle 计时器）
            return idleVariant_ ? "clawd-idle-reading.gif" : "idle.gif";
        case AgentState::Thinking:     return "clawd-thinking.gif";
        case AgentState::ToolUse: {     // working：busy_0..3 轮换（见 tick busy 计时器）
            static const char* kBusy[4] = {"busy_0.gif", "busy_1.gif", "busy_2.gif", "busy_3.gif"};
            return kBusy[busyVariant_ & 3];
        }
        case AgentState::Approval:     return "attention.gif";
        case AgentState::Done:         return "celebrate.gif";
        case AgentState::Notification: return "clawd-notification.gif";
        case AgentState::Connecting:   return "clawd-carrying.gif";   // 未连上 cc-bridge
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

// 右上角：电量 %（最右，三色档）+ 会话计数角标 T/R（电量左侧）。NORMAL。
// 保留区清一次再画两项，避免电量位数变化(如 100%→85%)残留像素。
// openspec change cardputer-battery-indicator。
void drawBadge() {
    canvas.setTextSize(1);
    canvas.fillRect(canvasW - 66, 0, 66, 12, BG);   // 清右上保留区(~66px：电量+T/R)
    canvas.setTextDatum(top_right);
    int rightX = canvasW - 2;
    if (batPct_ >= 0) {                              // 电量(最右)，<0=unknown 不画
        char e[8];
        snprintf(e, sizeof(e), "%d%%", batPct_);
        uint16_t col = (batPct_ >= 50) ? 0x07E0     // 绿 ≥50%
                     : (batPct_ >= 20) ? 0xFFE0     // 黄 20–49%
                                       : 0xF800;    // 红 <20% 该充了
        canvas.setTextColor(col, BG);
        canvas.drawString(e, rightX, 2);
        rightX -= (int)strlen(e) * 6 + 5;           // 让出电量宽度 + 间隔
    }
    char b[16];                                     // T/R 会话角标(电量左侧)
    snprintf(b, sizeof(b), "%d/%d", badgeTotal_, badgeRunning_);  // 总会话/运行中
    canvas.setTextColor(CLAWD, BG);
    canvas.drawString(b, rightX, 2);
    canvas.setTextDatum(top_left);
}

// 多会话轮播：顶栏左会话标识 + [i/N]；钉态底部横幅（NORMAL）。openspec cardputer-session-rotation。
void drawSessionTag() {
    if (!online_) {                          // 未连上 cc-bridge：常驻连接提示
        canvas.setTextSize(1);
        canvas.setTextDatum(top_left);
        canvas.fillRect(0, 0, canvasW - 66, 13, BG);
        canvas.setTextColor(0x8410, BG);     // 与会话标识同款灰
        canvas.drawString("Connecting...", 2, 1);
        return;
    }
    if (rotTotal_ <= 0 || !rotTag_[0]) return;
    canvas.setFont(&fonts::efontCN_12);   // label 可能是中文 cmux 名
    canvas.setTextSize(1);
    canvas.setTextDatum(top_left);
    char line[52];
    snprintf(line, sizeof(line), "%s [%d/%d]", rotTag_, rotIdx_ + 1, rotTotal_);
    canvas.fillRect(0, 0, canvasW - 66, 13, BG);   // 清左侧条，留右上保留区(~66px：电量+T/R)
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

// 本机录音持久指示：顶栏左红点 + "REC mm:ss"。录音态取代会话标识/Connecting 那条左栏，
// 优先级更高（录音进行中最需要看到），画在同一左侧条区域（留出右上电量/角标保留区）。
void drawRecIndicator() {
    canvas.setFont(&fonts::Font0);
    canvas.setTextSize(1);
    canvas.setTextDatum(top_left);
    canvas.fillRect(0, 0, canvasW - 66, 13, BG);   // 清左侧条（同 drawSessionTag 的保留区）
    canvas.fillCircle(6, 6, 4, 0xF800);             // 红点
    uint32_t sec = recElapsedMs_ / 1000;
    char line[16];
    snprintf(line, sizeof(line), "REC %02u:%02u", (unsigned)(sec / 60), (unsigned)(sec % 60));
    canvas.setTextColor(0xF800, BG);                // 红
    canvas.drawString(line, 14, 2);
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
    // 标题计数 = 实际列出的行数（含 cursor/codex 等 ext 会话），不是 payload.total
    // （后者只算本机 claude，会跟列表对不上）。
    snprintf(title, sizeof(title), "SESSIONS (%d)", sessN_);
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
            // agent 标记：claude=黄 "cc"，cursor=灰蓝 "cu"，codex=绿 "cx"，
            // opencode=青 "oc"，kimi=紫 "ki"。颜色+文字双重区分。
            const char* agent = sess_[idx].agent;
            const char* atag; uint16_t rowCol;
            if (strcmp(agent, "cursor") == 0)      { atag = "cu"; rowCol = 0xCE59; }
            else if (strcmp(agent, "codex") == 0)  { atag = "cx"; rowCol = 0x07E5; }
            else if (strcmp(agent, "opencode") == 0) { atag = "oc"; rowCol = 0x05FF; }
            else if (strcmp(agent, "kimi") == 0)   { atag = "ki"; rowCol = 0x801F; }
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

// 笔记列表（NOTES，复刻 SESSIONS 滚动列表范式）。
// 标题 NOTES(N)，每行文件名；空列表 "(no notes)"。回放中顶栏 ▶ playing。
void drawNotes() {
    canvas.fillSprite(BG);
    canvas.setTextColor(CLAWD, BG);
    canvas.setTextSize(1);
    canvas.setFont(&fonts::efontCN_12);   // 中文文件名渲染
    canvas.setTextDatum(top_left);
    char title[24];
    snprintf(title, sizeof(title), "NOTES (%d)", notesN_);
    canvas.drawString(title, 6, 2);
    const int rowH = 14, top = 16, rows = (canvasH - top) / rowH;  // efontCN_12 行高
    if (notesN_ == 0) {
        canvas.setTextColor(0x8410, BG);
        canvas.drawString("(no notes)", 6, top);
    } else {
        for (int r = 0; r < rows; r++) {
            int idx = notesScroll_ + r;
            if (idx >= notesN_) break;
            bool sel = (idx == notesSel_);
            int y = top + r * rowH;
            if (sel) canvas.fillRect(0, y - 1, canvasW, rowH, 0x2945);  // 选中行高亮底
            bool playing = (idx == notesPlaying_);
            canvas.setTextColor(sel ? TFT_WHITE : (playing ? 0x07E0 : 0xCE59),
                                sel ? 0x2945 : BG);
            char row[40];
            snprintf(row, sizeof(row), "%c%c %s", sel ? '>' : ' ',
                     playing ? 0x10 /* ▶ */ : ' ', notesNames_[idx]);
            canvas.drawString(row, 6, y);
        }
    }
    if (notesScroll_ > 0) canvas.drawString("^", canvasW - 10, top);
    if (notesScroll_ + rows < notesN_) canvas.drawString("v", canvasW - 10, canvasH - rowH);
    canvas.setFont(&fonts::Font0);   // 复位默认字体
}

// 键盘笔记本（NOTEBOOK）— 简易文本编辑器。顶栏标题 + [x/600] 字符数。
// 左对齐单色文本，光标闪烁实心方块。单行最多 ~39 字符，超出自动折行。
void drawNotebook() {
    canvas.fillSprite(BG);
    canvas.setTextSize(1);
    canvas.setTextDatum(top_left);

    // 顶栏：标题 + 字符数
    canvas.setTextColor(CLAWD, BG);
    char title[24];
    snprintf(title, sizeof(title), "NOTEBOOK [%d/%d]", nbLen_, NB_BUF_SIZE);
    canvas.drawString(title, 4, 1);
    canvas.drawFastHLine(0, 12, canvasW, CLAWD);

    // 文本渲染：逐字符画，遇 '\n' 换行，超宽折行。光标闪烁：实心块覆盖在字符上。
    const int L = 4, startY = 15, rowH = 12, maxX = canvasW - 4;
    int x = L, y = startY, charIdx = 0;
    bool blinkOn = (millis() / 400) % 2;  // ~400ms 闪烁周期

    canvas.setTextColor(TFT_WHITE, BG);
    for (int i = 0; i < nbLen_; i++) {
        // 在文本渲染后若遇到光标位且闪烁在位 → 画光标块
        if (i == nbCursor_ && blinkOn) {
            canvas.fillRect(x, y, 7, rowH, TFT_WHITE);
        }

        char c = nbBuf_[i];
        if (c == '\n') {
            x = L; y += rowH;
            charIdx++;
            if (y + rowH > canvasH) break;   // 超出屏幕不画
            continue;
        }

        // 折行
        if (x + 7 > maxX) { x = L; y += rowH; }
        if (y + rowH > canvasH) break;

        canvas.drawChar(c, x, y);
        x += 7;   // Font0 字符宽 ~6px + 1px spacing
        charIdx++;
    }

    // 光标在文本末尾
    if (nbCursor_ == nbLen_ && blinkOn) {
        canvas.fillRect(x, y, 7, rowH, TFT_WHITE);
    }

    // 底栏提示
    canvas.setTextColor(0x8410, BG);
    canvas.drawString("esc=save+exit", 4, canvasH - 12);
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

void setState(AgentState s) {
    // 重新进入 Idle 时从 idle.gif 开始，不沿用上次离开 Idle 时可能停留的 reading 变体。
    if (s == AgentState::Idle && baseState_ != AgentState::Idle) idleVariant_ = 0;
    baseState_ = s;
    if (ready && mode_ == NORMAL) applyTarget();
}
void setBadge(int total, int running) { badgeTotal_ = total; badgeRunning_ = running; }
// 电量 %：clamp 到 [-1,100]；<0 视为 unknown（drawBadge 不画）。openspec cardputer-battery-indicator
void setBattery(int pct) { batPct_ = (pct < 0) ? -1 : (int8_t)(pct > 100 ? 100 : pct); }
void setSessionTag(const char* tag, int idx, int total, bool pinned) {
    if (tag) utf8lcpy(rotTag_, tag, sizeof(rotTag_)); else rotTag_[0] = 0;
    rotIdx_ = idx; rotTotal_ = total; rotPinned_ = pinned;
}
void setToast(const char* text) {
    strncpy(toast_, text ? text : "", sizeof(toast_) - 1);
    toast_[sizeof(toast_) - 1] = 0;
    toastMs_ = 1500;
}
void setRecording(bool on, uint32_t elapsedMs) { recording_ = on; recElapsedMs_ = elapsedMs; }

void setSleeping(bool sleep) {
    if (sleep == sleeping_) return;
    sleeping_ = sleep;
    if (ready && mode_ == NORMAL) applyTarget();
}
// 纯存标志：GIF 由 main.cpp 的 setState 驱动，顶栏每 tick 重绘，无需在此重开 GIF。
void setOnline(bool on) { online_ = on; }
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
    if (mode_ != APPROVAL && mode_ != QUESTION && mode_ != NOTES && mode_ != NOTEBOOK) mode_ = SESSIONS;
}
void hideSessions() { if (mode_ == SESSIONS) { mode_ = NORMAL; strcpy(curFile, ""); applyTarget(); } }
void sessionsMove(int delta) {
    if (mode_ != SESSIONS || sessN_ == 0) return;
    // 循环导航：到最后一个再按下 → 回到第一个；第一个再按上 → 到最后一个。
    sessSel_ = ((sessSel_ + delta) % sessN_ + sessN_) % sessN_;
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

void showNotes(const char (*names)[32], uint8_t n, int8_t playingIdx) {
    notesN_ = n > 32 ? 32 : n;
    for (uint8_t i = 0; i < notesN_; i++) {
        strncpy(notesNames_[i], names[i], sizeof(notesNames_[i]) - 1);
        notesNames_[i][sizeof(notesNames_[i]) - 1] = 0;
    }
    notesSel_ = 0;
    notesScroll_ = 0;
    notesPlaying_ = playingIdx;
    if (mode_ != APPROVAL && mode_ != QUESTION) mode_ = NOTES;
}
void hideNotes() { if (mode_ == NOTES) { mode_ = NORMAL; strcpy(curFile, ""); applyTarget(); notesPlaying_ = -1; } }
void notesMove(int delta) {
    if (mode_ != NOTES || notesN_ == 0) return;
    notesSel_ = ((notesSel_ + delta) % notesN_ + notesN_) % notesN_;
    const int rowH = 14, top = 16, rows = (canvasH - top) / rowH;
    if (notesSel_ < notesScroll_) notesScroll_ = notesSel_;
    else if (notesSel_ >= notesScroll_ + rows) notesScroll_ = notesSel_ - rows + 1;
}
const char* notesSelected() {
    if (mode_ != NOTES || notesSel_ < 0 || notesSel_ >= notesN_) return "";
    return notesNames_[notesSel_];
}
bool notesVisible() { return mode_ == NOTES; }

// ── NOTEBOOK ──
void showNotebook() {
    nbLen_ = 0; nbCursor_ = 0;
    nbBuf_[0] = 0;
    if (mode_ != APPROVAL && mode_ != QUESTION) mode_ = NOTEBOOK;
}
void hideNotebook(bool save) {
    if (mode_ == NOTEBOOK) { mode_ = NORMAL; strcpy(curFile, ""); applyTarget(); }
}
void notebookKey(char c) {
    if (mode_ != NOTEBOOK) return;
    if (nbLen_ >= NB_BUF_SIZE - 1) return;
    // 光标在中间时：后移后续字符
    if (nbCursor_ < nbLen_) {
        memmove(nbBuf_ + nbCursor_ + 1, nbBuf_ + nbCursor_, nbLen_ - nbCursor_);
    }
    nbBuf_[nbCursor_] = c;
    nbLen_++;
    nbCursor_++;
    nbBuf_[nbLen_] = 0;
}
void notebookBackspace() {
    if (mode_ != NOTEBOOK) return;
    if (nbCursor_ <= 0) return;
    // 光标前移 + 删除前一个字符
    if (nbCursor_ < nbLen_) {
        memmove(nbBuf_ + nbCursor_ - 1, nbBuf_ + nbCursor_, nbLen_ - nbCursor_);
    }
    nbCursor_--;
    nbLen_--;
    nbBuf_[nbLen_] = 0;
}
void notebookNewline() {
    notebookKey('\n');
}
const char* notebookText() {
    return (mode_ == NOTEBOOK) ? nbBuf_ : "";
}
bool notebookVisible() { return mode_ == NOTEBOOK; }

void showHelp() { if (mode_ != APPROVAL && mode_ != QUESTION && mode_ != SESSIONS && mode_ != NOTES && mode_ != NOTEBOOK) mode_ = HELP; }
void hideHelp() { if (mode_ == HELP) { mode_ = NORMAL; strcpy(curFile, ""); applyTarget(); } }
bool helpVisible() { return mode_ == HELP; }

void tick(uint32_t dtMs) {
    if (!ready) return;

    if (mode_ == APPROVAL) { drawApproval(); canvas.pushSprite(0, 0); return; }
    if (mode_ == QUESTION) { drawQuestion(); canvas.pushSprite(0, 0); return; }
    if (mode_ == SESSIONS) { drawSessions(); canvas.pushSprite(0, 0); return; }
    if (mode_ == NOTES)    { drawNotes();    canvas.pushSprite(0, 0); return; }
    if (mode_ == NOTEBOOK) { drawNotebook(); canvas.pushSprite(0, 0); return; }
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
    // Idle 态：大多数时候 idle.gif，偶尔（低概率、非固定周期）飘一下 idle-reading 再切回，
    // 呼应 upstream "Idle (random)" 语义，不做 busy_0..3 那种均匀顺序轮播。
    // 首次判定延迟/命中率刻意调高：main.cpp 的 setSleeping 会在 Idle+静止 30s 后转
    // sleep（!sleeping_ 会挡住本判定），窗口只有 ~30s，太保守会被 sleep 抢先盖掉，
    // 真机验证过一次（10s 延迟+20% 命中率在 30s 内几乎看不到），故收紧到 3s 起判、
    // 6s 一轮、40% 命中，5 轮内至少命中一次的概率 ~92%。openspec cardputer-idle-variety。
    static bool prevIdle = false;
    bool nowIdle = (baseState_ == AgentState::Idle && !sleeping_ && reactionMs_ <= 0);
    if (nowIdle) {
        if (!prevIdle) idleNextMs_ = now + 3000;        // 刚进 idle：3s 后开始判定
        else if (now >= idleNextMs_) {
            if (idleVariant_ == 0) {
                if ((uint32_t)random(100) < 40) {        // ~40% 命中才切到 reading
                    idleVariant_ = 1;
                    idleNextMs_ = now + 5000;            // reading 停留 ~5s
                } else {
                    idleNextMs_ = now + 6000;            // 没中，6s 后再判一次
                }
            } else {
                idleVariant_ = 0;                        // 到点必定切回 idle.gif
                idleNextMs_ = now + 6000;                // 统一判定节奏，见上方注释
            }
            applyTarget();
        }
    }
    prevIdle = nowIdle;
    if (now < nextFrameAt) return;
    int delayMs = 0;
    if (!gif.playFrame(false, &delayMs)) { gif.reset(); gif.playFrame(false, &delayMs); }
    drawBadge();
    if (toastMs_ > 0) { toastMs_ -= (int32_t)dtMs; drawToast(); }
    // 录音态：REC 指示占据左侧条，优先于会话标识/Connecting（录音进行中最需可见）。
    if (recording_) drawRecIndicator();
    else drawSessionTag();   // 顶栏会话标识 + 钉态横幅（盖在 toast 之上）
    canvas.pushSprite(0, 0);
    nextFrameAt = now + (delayMs > 0 ? delayMs : 100);
}

}  // namespace clawd
