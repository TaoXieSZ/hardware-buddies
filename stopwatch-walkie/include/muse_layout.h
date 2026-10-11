#pragma once

// Reading-page text layout for the round 466 px screen (direct-to-Muse build): pure, native-testable.
// Spec: openspec/changes/add-stopwatch-muse-direct/specs/stopwatch-muse-direct/spec.md (Reading pages)
// The preview script tools/muse-fonts/preview_reading.py mirrors these rules; keep them in step.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <initializer_list>
#include <string>
#include <vector>

namespace stopwatch {

/** Advance width of `cp` at `px`, or 0 when the font has no glyph for it. */
using MuseAdvance = std::function<int(int px, uint32_t cp)>;

struct MuseRound {
    int cx = 233;
    int cy = 233;
    int radius = 190;  // text stays well inside the status ring (radius 216)
};

struct MuseTextBlock {
    int px = 0;
    int top = 0;  // y of the first line box
    int line_height = 0;
    std::vector<std::string> lines;
    std::vector<int> widths;
    bool truncated = false;
};

constexpr uint32_t kMuseMissingGlyph = 0x25A1;  // "□" stands in for characters the font lacks
constexpr uint32_t kMuseEllipsis = 0x2026;

inline int museLineHeight(int px) { return px * 5 / 4; }

/** Usable width of the line box [y0, y1) inside the circle. */
inline int museChordWidth(const MuseRound& round, int y0, int y1)
{
    const int d = std::max(std::abs(y0 - round.cy), std::abs(y1 - round.cy));
    if (d >= round.radius) return 0;
    return 2 * static_cast<int>(std::sqrt(static_cast<double>(round.radius * round.radius - d * d)));
}

namespace detail {

inline void appendUtf8(std::string& out, uint32_t cp)
{
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

inline std::vector<uint32_t> decodeUtf8(const std::string& text)
{
    std::vector<uint32_t> cps;
    for (size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        const int extra = c < 0x80 ? 0 : (c >> 5) == 0x6 ? 1 : (c >> 4) == 0xE ? 2 : (c >> 3) == 0x1E ? 3 : -1;
        if (extra < 0 || i + static_cast<size_t>(extra) >= text.size() + (extra == 0 ? 1 : 0)) {
            ++i;  // stray byte: skip it
            continue;
        }
        uint32_t cp = extra == 0 ? c : c & (0x3F >> extra);
        for (int k = 1; k <= extra; ++k) cp = (cp << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3F);
        cps.push_back(cp);
        i += extra + 1;
    }
    return cps;
}

/** Characters that never start a line: they hang at the end of the previous one instead. */
inline bool hangs(uint32_t cp)
{
    static constexpr uint32_t kHang[] = {0xFF0C, 0x3002, 0x3001, 0xFF01, 0xFF1F, 0xFF1B, 0xFF1A, 0xFF09, 0x300B,
                                         0x300D, 0x300F, 0x201D, 0x2019, 0x2026, 0xFF05, ',', '.', '!', '?',
                                         ';', ':', ')', '%'};
    return std::find(std::begin(kHang), std::end(kHang), cp) != std::end(kHang);
}

struct Token {
    std::vector<uint32_t> cps;
    int width = 0;
    bool space = false;
    bool hang = false;
};

inline bool asciiWordChar(uint32_t cp) { return cp > 0x20 && cp < 0x7F && !hangs(cp); }

/** Whitespace runs collapse to one space; ASCII words stay whole; every other character is its own token. */
inline std::vector<Token> tokenize(const std::string& text, int px, const MuseAdvance& advance)
{
    std::vector<Token> tokens;
    auto width = [&](uint32_t& cp) {
        int w = advance(px, cp);
        if (w <= 0 && cp != ' ') {
            cp = kMuseMissingGlyph;
            w = advance(px, cp);
        }
        return std::max(w, 0);
    };
    for (uint32_t cp : decodeUtf8(text)) {
        if (cp == ' ' || cp == '\n' || cp == '\r' || cp == '\t' || cp == 0x3000) {
            if (!tokens.empty() && !tokens.back().space) {
                Token t;
                t.space = true;
                t.cps.push_back(' ');
                t.width = advance(px, ' ');
                tokens.push_back(t);
            }
            continue;
        }
        if (cp < 0x20 || (cp >= 0xFE00 && cp <= 0xFE0F) || cp == 0x200D) continue;  // controls, emoji joiners
        const bool word = asciiWordChar(cp);
        if (word && !tokens.empty() && !tokens.back().space && !tokens.back().cps.empty() &&
            asciiWordChar(tokens.back().cps.back())) {
            tokens.back().width += width(cp);
            tokens.back().cps.push_back(cp);
            continue;
        }
        Token t;
        t.width = width(cp);
        t.cps.push_back(cp);
        t.hang = hangs(cp);
        tokens.push_back(t);
    }
    while (!tokens.empty() && tokens.back().space) tokens.pop_back();
    return tokens;
}

/** Greedy fill of one line from `next`; words wider than the line are split by character. */
inline void fillLine(std::vector<Token>& tokens, size_t& next, int limit, int px, const MuseAdvance& advance,
                     std::string& line, int& used)
{
    line.clear();
    used = 0;
    while (next < tokens.size() && tokens[next].space) ++next;
    while (next < tokens.size()) {
        Token& t = tokens[next];
        if (t.space) {
            size_t peek = next + 1;
            if (peek < tokens.size() && used + t.width + tokens[peek].width > limit) break;  // no dangling space
        }
        if (used + t.width <= limit || (t.hang && !line.empty())) {
            for (uint32_t cp : t.cps) appendUtf8(line, cp);
            used += t.width;
            ++next;
            continue;
        }
        if (!line.empty() && t.cps.size() == 1) break;
        if (!line.empty() && t.width <= limit) break;
        // A word longer than the remaining space on an empty line, or longer than any line: split it.
        size_t taken = 0;
        while (taken < t.cps.size()) {
            const int w = advance(px, t.cps[taken]);
            if (used + w > limit && !(line.empty() && taken == 0)) break;
            appendUtf8(line, t.cps[taken]);
            used += w;
            ++taken;
        }
        t.cps.erase(t.cps.begin(), t.cps.begin() + static_cast<long>(taken));
        t.width = 0;
        for (uint32_t cp : t.cps) t.width += advance(px, cp);
        if (t.cps.empty()) ++next;
        break;
    }
}

}  // namespace detail

/**
 * Lay `text` out in the band [band_top, band_bottom), vertically centred, each line as wide as the circle allows.
 * Tries each size in `sizes` (largest first) with as few lines as possible; only the smallest size may truncate,
 * ending the last line with "…".
 */
inline MuseTextBlock museLayout(const std::string& text, std::initializer_list<int> sizes, int band_top, int band_bottom,
                                const MuseRound& round, const MuseAdvance& advance)
{
    MuseTextBlock block;
    int last_px = 0;
    for (int px : sizes) {
        last_px = px;
        const int lh = museLineHeight(px);
        const int max_lines = std::max(1, (band_bottom - band_top) / lh);
        for (int n = 1; n <= max_lines; ++n) {
            // `shrink` narrows every line equally; the largest that still fits n lines balances them (no orphans).
            auto attempt = [&](int shrink, MuseTextBlock& out) {
                std::vector<detail::Token> tokens = detail::tokenize(text, px, advance);
                out = MuseTextBlock();
                out.px = px;
                out.top = (band_top + band_bottom) / 2 - n * lh / 2;
                out.line_height = lh;
                size_t next = 0;
                for (int i = 0; i < n && next < tokens.size(); ++i) {
                    std::string line;
                    int used = 0;
                    const int limit = museChordWidth(round, out.top + i * lh, out.top + (i + 1) * lh) - shrink;
                    detail::fillLine(tokens, next, std::max(limit, px), px, advance, line, used);
                    out.lines.push_back(line);
                    out.widths.push_back(used);
                }
                while (next < tokens.size() && tokens[next].space) ++next;
                return next >= tokens.size();
            };
            MuseTextBlock best;
            if (!attempt(0, best)) continue;
            MuseTextBlock narrower;
            for (int shrink = px / 2; n > 1 && attempt(shrink, narrower); shrink += px / 2) best = narrower;
            return best;
        }
    }
    // Nothing fits: the smallest size, all lines, last line cut with "…".
    const int px = last_px;
    const int lh = museLineHeight(px);
    const int n = std::max(1, (band_bottom - band_top) / lh);
    std::vector<detail::Token> tokens = detail::tokenize(text, px, advance);
    block.px = px;
    block.line_height = lh;
    block.top = (band_top + band_bottom) / 2 - n * lh / 2;
    block.truncated = true;
    size_t next = 0;
    for (int i = 0; i < n; ++i) {
        const int limit = museChordWidth(round, block.top + i * lh, block.top + (i + 1) * lh);
        const bool last = i == n - 1;
        const int ellipsis = advance(px, kMuseEllipsis);
        std::string line;
        int used = 0;
        detail::fillLine(tokens, next, last ? limit - ellipsis : limit, px, advance, line, used);
        if (last) {
            std::vector<uint32_t> cps = detail::decodeUtf8(line);
            while (!cps.empty() && (cps.back() == ' ' || detail::hangs(cps.back()) || used + ellipsis > limit)) {
                used -= advance(px, cps.back());
                cps.pop_back();
            }
            line.clear();
            for (uint32_t cp : cps) detail::appendUtf8(line, cp);
            detail::appendUtf8(line, kMuseEllipsis);
            used += ellipsis;
        }
        block.lines.push_back(line);
        block.widths.push_back(used);
    }
    return block;
}

/** One line no wider than `limit`, cut with "…" when it does not fit (labels above the reading area). */
inline std::string museClipLine(const std::string& text, int px, int limit, const MuseAdvance& advance, int* width = nullptr)
{
    std::vector<detail::Token> tokens = detail::tokenize(text, px, advance);
    std::vector<uint32_t> cps;
    int used = 0;
    for (const detail::Token& t : tokens) {
        for (uint32_t cp : t.cps) {
            cps.push_back(cp);
            used += advance(px, cp);
        }
    }
    if (used > limit) {
        const int ellipsis = advance(px, kMuseEllipsis);
        while (!cps.empty() && used + ellipsis > limit) {
            used -= advance(px, cps.back());
            cps.pop_back();
        }
        cps.push_back(kMuseEllipsis);
        used += ellipsis;
    }
    std::string out;
    for (uint32_t cp : cps) detail::appendUtf8(out, cp);
    if (width) *width = used;
    return out;
}

}  // namespace stopwatch
