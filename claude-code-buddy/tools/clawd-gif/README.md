# clawd-gif — HD avatar pack pipeline for the Tab5

Generates the Tab5 dashboard avatar GIFs (`data/characters/clawd/*.gif`) from
the [rullerzhou-afk/clawd-on-desk](https://github.com/rullerzhou-afk/clawd-on-desk)
ready-made GIF set. The on-device pack is **gitignored** — this directory (the
script + mapping) is the source of truth; rerun to regenerate.

## Usage

```bash
tools/clawd-gif/build-pack.sh
pio run -e m5stack-tab5 -t uploadfs --upload-port /dev/cu.usbmodemXXXX
```

(Requires `curl`, ImageMagick `magick`, `python3`. Stop whatever holds the
serial port — usually cursor-bridge — before uploadfs.)

## State → source GIF mapping

The firmware (`src/tab5/avatar.cpp`) plays GIFs by filename. `STATE_FILES`
covers the 5 agent states; `MOOD_FILES` adds sleep/heart (see `avatar.h`
`AV_MOOD_*`). Mapping lives in `build-pack.sh`:

| firmware file   | agent state / mood        | clawd-on-desk GIF          |
|-----------------|---------------------------|----------------------------|
| `idle.gif`      | ST_IDLE                   | `clawd-idle`               |
| `busy_0.gif`    | ST_BUSY                   | `clawd-thinking`           |
| `attention.gif` | ST_ATTN (waiting on user) | `clawd-notification`       |
| `celebrate.gif` | ST_DONE                   | `clawd-happy`              |
| `dizzy.gif`     | ST_ERR                    | `clawd-error`              |
| `sleep.gif`     | mood AV_MOOD_SLEEP (nap)  | `clawd-sleeping`           |
| `heart.gif`     | mood AV_MOOD_HEART (pet)  | `clawd-headphones-groove`  |

## Why the processing (hard-won — don't regress)

`recrop2.sh` does two non-obvious things:

1. **Single global palette (`+remap`).** The device's bitbank2 AnimatedGIF
   decoder (`GIF_PALETTE_RGB565_BE`) renders **inverted/wrong colors** when a
   GIF has per-frame *local* color tables (what ffmpeg/ImageMagick optimizers
   emit by default). Forcing one global palette fixes it.
2. **Body-size normalization.** The avatar box is small (~148 px). Cropping each
   GIF to its full content bbox makes Clawd jump size between states (props like
   the bulb/bubble/sparkles inflate the bbox). Instead we mask the **coral body**
   (`#d97757`), measure *its* bbox, and scale so the body is a constant size
   across all states; props extend into the margin.

Also avoid: wide "desk scene" poses (`clawd-typing`, `clawd-building`) — the
character ends up tiny; and the `clawd-mini-*` sprites — they're a different
scale and won't match the full poses.

## Alternative (abandoned): SVG → GIF via headless Chrome

The repo also ships animated **SVGs** (`assets/svg/`). Rasterizing them with a
headless-Chrome frame-capture pipeline worked but produced subtly off colors
(color profile / palettegen / black-bg compositing), so we use the ready GIFs
instead. If you ever need the SVG route, the approach was: puppeteer-core seeks
`document.getAnimations()` currentTime per frame → screenshot → assemble GIF.
