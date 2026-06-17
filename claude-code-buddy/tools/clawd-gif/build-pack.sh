#!/bin/bash
# build-pack.sh — (re)generate the clawd HD avatar pack for the Tab5 from the
# clawd-on-desk repo's ready-made GIFs. Downloads each mapped source GIF,
# body-normalizes it via recrop2.sh (consistent character size + global palette),
# and installs into data/characters/clawd/. Then run uploadfs.
#
#   tools/clawd-gif/build-pack.sh
#   pio run -e m5stack-tab5 -t uploadfs --upload-port /dev/cu.usbmodemXXXX
#
# Requires: curl, ImageMagick (magick), python3.
# Note: data/ is gitignored — the GIFs are NOT committed; this script + the
# mapping below ARE the source of truth, rerun to regenerate.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
DEST="$HERE/../../data/characters/clawd"
BASE="https://raw.githubusercontent.com/rullerzhou-afk/clawd-on-desk/main/assets/gif"
TMP=$(mktemp -d)

# firmware state (src/tab5/avatar.cpp STATE_FILES + mood) -> clawd-on-desk GIF.
# Keep character-centric poses (avatar box is ~148px); avoid wide "desk scene"
# poses (clawd-typing/building) and the mini-* sprites (different scale).
MAP=(
  "idle:clawd-idle"                  # ST_IDLE
  "busy_0:clawd-thinking"            # ST_BUSY  (thought bubble; not the desk scene)
  "attention:clawd-notification"     # ST_ATTN  (lightbulb)
  "celebrate:clawd-happy"            # ST_DONE  (sparkles)
  "dizzy:clawd-error"                # ST_ERR   (XX eyes)
  "sleep:clawd-sleeping"             # mood: idle-nap (AV_MOOD_SLEEP)
  "heart:clawd-headphones-groove"    # mood: pet (AV_MOOD_HEART) — headphones + ^^ eyes
)
for pair in "${MAP[@]}"; do
  st="${pair%%:*}"; src="${pair#*:}"
  curl -fsSL "$BASE/$src.gif" -o "$TMP/$src.gif"
  "$HERE/recrop2.sh" "$TMP/$src.gif" "$DEST/$st.gif"
done
rm -rf "$TMP"
echo "installed pack -> $DEST"
echo "next: pio run -e m5stack-tab5 -t uploadfs --upload-port <port>"
