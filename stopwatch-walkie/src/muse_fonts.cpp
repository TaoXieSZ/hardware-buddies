#include "muse_fonts.h"

#if defined(WALKIE_MUSE_DIRECT)

#include <esp_partition.h>

#include <algorithm>
#include <cstring>

namespace stopwatch {
namespace {

constexpr int kMaxFonts = 4;

// lgfx reads glyph records and bitmaps through this; a small read-ahead keeps the per-glyph header
// lookups from hitting flash one 24-byte record at a time.
struct PartitionData : public lgfx::DataWrapper {
    const esp_partition_t* part = nullptr;
    uint32_t base = 0;
    uint32_t length = 0;
    uint32_t pos = 0;
    uint8_t cache[512];
    uint32_t cache_at = UINT32_MAX;
    uint32_t cache_len = 0;

    int read(uint8_t* buf, uint32_t len) override
    {
        if (pos >= length) return 0;
        len = std::min(len, length - pos);
        if (len > sizeof(cache)) {
            if (esp_partition_read(part, base + pos, buf, len) != ESP_OK) return 0;
        } else {
            if (cache_at == UINT32_MAX || pos < cache_at || pos + len > cache_at + cache_len) {
                cache_at = pos;
                cache_len = std::min<uint32_t>(sizeof(cache), length - pos);
                if (esp_partition_read(part, base + cache_at, cache, cache_len) != ESP_OK) {
                    cache_at = UINT32_MAX;
                    return 0;
                }
            }
            std::memcpy(buf, cache + (pos - cache_at), len);
        }
        pos += len;
        return static_cast<int>(len);
    }
    void skip(int32_t offset) override { pos += offset; }
    bool seek(uint32_t offset) override
    {
        pos = offset;
        return pos <= length;
    }
    void close() override {}
    int32_t tell() override { return static_cast<int32_t>(pos); }
};

struct LoadedFont {
    int px = 0;
    PartitionData data;
    lgfx::VLWfont font;
};

LoadedFont g_fonts[kMaxFonts];
int g_count = 0;

}  // namespace

bool museFontsBegin()
{
    const esp_partition_t* part =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "fonts");
    if (part == nullptr) {
        Serial.println("[fonts] no fonts partition; reading pages use efont");
        return false;
    }
    uint8_t head[8 + kMaxFonts * 12];
    if (esp_partition_read(part, 0, head, sizeof(head)) != ESP_OK || std::memcmp(head, "MFN1", 4) != 0) {
        Serial.println("[fonts] fonts partition is empty; flash fonts/muse_fonts.bin (see README)");
        return false;
    }
    uint32_t count = 0;
    std::memcpy(&count, head + 4, 4);
    count = std::min<uint32_t>(count, kMaxFonts);
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t entry[3];
        std::memcpy(entry, head + 8 + i * 12, sizeof(entry));
        if (entry[1] + entry[2] > part->size) continue;
        LoadedFont& f = g_fonts[g_count];
        f.px = static_cast<int>(entry[0]);
        f.data.part = part;
        f.data.base = entry[1];
        f.data.length = entry[2];
        const uint32_t started = millis();
        if (!f.font.loadFont(&f.data)) continue;
        Serial.printf("[fonts] %dpx glyphs=%u ms=%lu\n", f.px, static_cast<unsigned>(f.font.gCount),
                      static_cast<unsigned long>(millis() - started));
        ++g_count;
    }
    return g_count > 0;
}

const lgfx::VLWfont* museFont(int px)
{
    const lgfx::VLWfont* best = nullptr;
    int best_px = 0;
    for (int i = 0; i < g_count; ++i) {
        if (g_fonts[i].px <= px && g_fonts[i].px > best_px) {
            best = &g_fonts[i].font;
            best_px = g_fonts[i].px;
        }
    }
    return best;
}

int museAdvance(int px, uint32_t cp)
{
    const lgfx::VLWfont* font = museFont(px);
    if (font == nullptr || cp > 0xFFFF) return 0;
    lgfx::FontMetrics metrics;
    if (!font->updateFontMetric(&metrics, static_cast<uint16_t>(cp))) return 0;
    return metrics.x_advance;
}

}  // namespace stopwatch

#endif
