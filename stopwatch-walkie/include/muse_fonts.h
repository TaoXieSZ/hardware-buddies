#pragma once

// Anti-aliased CJK reading fonts for the direct-to-Muse build, read on demand from the `fonts` flash
// partition (partitions_muse.csv; image built by tools/muse-fonts/make_fonts.py). Nothing is copied to RAM
// except each font's glyph index (~10 bytes per glyph, in PSRAM).

#if defined(WALKIE_MUSE_DIRECT)

#include <M5Unified.h>
#include <cstdint>

namespace stopwatch {

/** Finds the partition and loads every size in it; false (and reading pages fall back to efont) if absent. */
bool museFontsBegin();
/** The loaded font closest to `px` from below, or nullptr when the partition is missing. */
const lgfx::VLWfont* museFont(int px);
/** Advance width of `cp` at `px`; 0 when that font has no glyph (the layout substitutes "□"). */
int museAdvance(int px, uint32_t cp);

}  // namespace stopwatch

#endif
