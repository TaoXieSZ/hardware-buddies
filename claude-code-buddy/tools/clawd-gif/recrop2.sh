#!/bin/bash
# recrop2.sh <src.gif> <out.gif> — normalize a clawd-on-desk GIF for the Tab5
# avatar box. Normalizes by Clawd's BODY size (mask the coral body, ignore
# yellow/white props) so the character is the SAME size in every state; props
# (bulb / bubble / sparkles) extend into the margin. Forces a single global
# palette (+remap) — the device's AnimatedGIF decoder shows wrong/inverted
# colors with per-frame local color tables.
set -e
src="$1"; out="$2"
TARGET=100   # body max-dim -> px in the 220 box (~45%); rest is margin for props
FINAL=220
PAD=500
tmp=$(mktemp -d)
magick "$src" -coalesce "$tmp/co.miff"
magick "$tmp/co.miff" -evaluate-sequence max "$tmp/u.png"
# body bbox = trim box of coral-only mask (props blackened)
geom=$(magick "$tmp/u.png" -fuzz 35% -fill black +opaque '#d97757' -fuzz 10% -format "%@" info:)
read win cropx cropy <<EOF
$(python3 - "$geom" "$TARGET" "$FINAL" "$PAD" <<'PY'
import sys
geom, TARGET, FINAL, PAD = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4])
wh, xy = geom.split('+', 1)
bw, bh = [int(v) for v in wh.split('x')]
bx, by = [int(v) for v in xy.split('+')]
bmax = max(bw, bh) or 1
win = round(FINAL * bmax / TARGET)
cx, cy = bx + bw // 2, by + bh // 2
print(win, cx - win // 2 + PAD, cy - win // 2 + PAD)
PY
)
EOF
magick "$tmp/co.miff" -coalesce -bordercolor black -border ${PAD}x${PAD} \
  -crop "${win}x${win}+${cropx}+${cropy}" +repage \
  -resize ${FINAL}x${FINAL} +remap "$out"
echo "$(basename "$out"): body=$geom win=$win frames=$(magick identify "$out" | wc -l | tr -d ' ') $(wc -c <"$out")B"
rm -rf "$tmp"
