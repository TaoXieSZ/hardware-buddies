"""Inline Clawd character per ship.

Three renderer flavours, picked automatically:

  - PackRenderer   — sprite pack (MIT) bundled at agent_fleet/packs/. Pixel-art
                     16×16 mascot with per-state animation frames, rendered as
                     truecolor half-block ANSI. ZERO external assets needed,
                     works in any truecolor terminal (including cmux). This is
                     the default.
  - KittyRenderer  — Kitty graphics protocol (Ghostty.app, kitty, WezTerm,
                     iTerm2 ≥ 3.5) reading clawd-on-desk GIFs. Higher-res but
                     requires user-installed clawd-on-desk repo + Pillow.
  - BlockArtRenderer — truecolor + ▀ half-block, reading clawd-on-desk GIFs.
                     Same fidelity tier as PackRenderer but uses the larger
                     clawd-on-desk artwork. Requires Pillow + clawd-on-desk.

Configuration (env):
  AGENT_FLEET_CLAWD=0          force off (no card image)
  AGENT_FLEET_CLAWD=pack       force bundled pixel-buddy pack (default)
  AGENT_FLEET_CLAWD=kitty      force Kitty graphics renderer
  AGENT_FLEET_CLAWD=block      force block-art renderer
  unset                        auto: pack > (kitty if supported) > block > none
  AGENT_FLEET_CLAWD_PACK=…     override bundled pack file path
  AGENT_FLEET_CLAWD_ASSETS=…   override clawd-on-desk gif/ path
                               (default: ~/OpenSourceProjects/clawd-on-desk/assets/gif)

The bundled pixel-buddy pack is from
https://github.com/TeXmeijin/claude-code-mascot-statusline (MIT). See
agent_fleet/packs/LICENSE.pixel-buddy.txt for the upstream notice.
"""
from __future__ import annotations

import io
import json
import os
import time
from base64 import standard_b64encode
from pathlib import Path
from typing import Optional

# State → GIF filename inside clawd-on-desk/assets/gif/.
_STATE_GIF: dict[str, str] = {
    "idle":         "clawd-idle.gif",
    "thinking":     "clawd-thinking.gif",
    "building":     "clawd-building.gif",
    "typing":       "clawd-typing.gif",
    "happy":        "clawd-happy.gif",
    "juggling":     "clawd-juggling.gif",
    "conducting":   "clawd-conducting.gif",
    "error":        "clawd-error.gif",
    "notification": "clawd-notification.gif",
    "sweeping":     "clawd-sweeping.gif",
    "carrying":     "clawd-carrying.gif",
    "sleeping":     "clawd-sleeping.gif",
}

DEFAULT_ASSETS_PATH = (
    Path.home() / "OpenSourceProjects" / "clawd-on-desk" / "assets" / "gif"
)


def state_for(activity: str, response: str, prompt: str, recap: str) -> str:
    """Map agent-fleet pane signals to a Clawd animation state."""
    if activity:
        v = activity.lower()
        if any(x in v for x in ("brew", "cogit", "ponder", "think", "musing")):
            return "thinking"
        if any(x in v for x in ("cook", "crunch", "build", "compil", "kneading")):
            return "building"
        if any(x in v for x in ("juggl", "spawn", "subagent", "delegat")):
            return "juggling"
        if any(x in v for x in ("conduct", "orchestr")):
            return "conducting"
        return "thinking"
    if response:
        return "happy"
    if prompt:
        return "typing"
    if recap:
        return "sleeping"
    return "idle"


def assets_dir() -> Optional[Path]:
    """Where clawd-on-desk's GIF assets live, or None if not findable."""
    custom = os.environ.get("AGENT_FLEET_CLAWD_ASSETS")
    p = Path(custom) if custom else DEFAULT_ASSETS_PATH
    if not p.is_dir():
        return None
    if not any((p / name).is_file() for name in _STATE_GIF.values()):
        return None
    return p


# ─── terminal capability detection ─────────────────────────────────────

def _has_truecolor() -> bool:
    """Heuristic: does this terminal support 24-bit foreground/background?"""
    if os.environ.get("COLORTERM", "").lower() in ("truecolor", "24bit"):
        return True
    # Most modern terminals do (Ghostty, kitty, iTerm2, WezTerm, Terminal.app
    # on macOS, gnome-terminal, cmux). Default optimistic.
    term = os.environ.get("TERM", "")
    if term in ("xterm-kitty", "xterm-ghostty", "xterm-256color", "screen-256color"):
        return True
    return False


def _has_kitty_graphics() -> bool:
    """Does the host terminal pass through the Kitty graphics protocol?

    cmux self-reports `TERM_PROGRAM=ghostty` but silently consumes the
    APC `_G` escapes (no image, no garbage — just a blank gap). We detect
    cmux via its `CMUX_SURFACE_ID` env var and exclude it.
    """
    # cmux fakes ghostty but doesn't pass kitty graphics through.
    if os.environ.get("CMUX_SURFACE_ID"):
        return False
    term_program = os.environ.get("TERM_PROGRAM", "")
    if term_program in ("ghostty", "WezTerm", "kitty"):
        return True
    if os.environ.get("TERM") in ("xterm-kitty", "xterm-ghostty"):
        return True
    if term_program == "iTerm.app":
        return True
    return False


# ─── shared frame loader ───────────────────────────────────────────────

def _open_cropped_rgba(gif_path: Path):
    """Open frame 0 of a GIF, convert to RGBA, and crop to the alpha
    bounding box so the character fills the canvas instead of being
    centred in a sea of transparent pixels. Returns a PIL Image or None.
    """
    try:
        from PIL import Image
    except ImportError:
        return None
    try:
        with Image.open(gif_path) as im:
            im.seek(0)
            im2 = im.convert("RGBA")
            bbox = im2.getbbox()      # smallest box containing non-zero pixels
            if bbox:
                im2 = im2.crop(bbox)
            return im2
    except Exception:
        return None


def _load_frame0_pixels(gif_path: Path, w: int, h: int):
    """Crop + resize frame 0 to `w × h`, return RGBA pixel list or None."""
    try:
        from PIL import Image  # for Image.LANCZOS
    except ImportError:
        return None
    im = _open_cropped_rgba(gif_path)
    if im is None:
        return None
    try:
        return list(im.resize((w, h), Image.LANCZOS).getdata())
    except Exception:
        return None


def _load_frame0_png(gif_path: Path) -> Optional[bytes]:
    """Frame 0 (bbox-cropped) as PNG bytes, or None."""
    im = _open_cropped_rgba(gif_path)
    if im is None:
        return None
    try:
        buf = io.BytesIO()
        im.save(buf, format="PNG")
        return buf.getvalue()
    except Exception:
        return None


# ─── Kitty graphics renderer ───────────────────────────────────────────

class KittyRenderer:
    """Real-pixel Clawd via Kitty graphics protocol APC sequences."""

    def __init__(self, assets: Path, cols: int = 16, rows: int = 8):
        self.assets = assets
        self.cols = cols
        self.rows = rows
        self.indent_cols = cols + 2
        self._png: dict[str, bytes] = {}
        self._image_id: dict[str, int] = {}
        self._transmitted: set[int] = set()
        self._next_id = 1000

    def preload(self, states=None):
        for st in states or list(_STATE_GIF):
            self._png_for(st)

    def _png_for(self, state: str) -> Optional[bytes]:
        if state in self._png:
            return self._png[state]
        gif_name = _STATE_GIF.get(state)
        if not gif_name:
            return None
        png = _load_frame0_png(self.assets / gif_name)
        if png is not None:
            self._png[state] = png
        return png

    def _id_for(self, state: str) -> int:
        if state not in self._image_id:
            self._image_id[state] = self._next_id
            self._next_id += 1
        return self._image_id[state]

    def _image_escape(self, state: str) -> str:
        png = self._png_for(state)
        if not png:
            return ""
        img_id = self._id_for(state)
        if img_id in self._transmitted:
            return (f"\x1b_Ga=p,i={img_id},c={self.cols},r={self.rows},"
                    f"C=1,q=2\x1b\\")
        payload = standard_b64encode(png).decode("ascii")
        CHUNK = 4096
        chunks = [payload[i:i + CHUNK] for i in range(0, len(payload), CHUNK)]
        parts: list[str] = []
        for idx, chunk in enumerate(chunks):
            more = 1 if idx < len(chunks) - 1 else 0
            if idx == 0:
                ctrl = (f"a=T,f=100,q=2,i={img_id},"
                        f"c={self.cols},r={self.rows},C=1,m={more}")
            else:
                ctrl = f"m={more}"
            parts.append(f"\x1b_G{ctrl};{chunk}\x1b\\")
        self._transmitted.add(img_id)
        return "".join(parts)

    def render_card(self, state: str, text_lines: list[str], gap: int = 2) -> list[str]:
        """Image at cursor + cursor-right-move ANSI to position text past it."""
        img = self._image_escape(state)
        move = f"\x1b[{self.indent_cols}C"
        first = text_lines[0] if text_lines else ""
        lines = [f"{img}{move}{first}"]
        lines += [f"{move}{t}" for t in text_lines[1:]]
        while len(lines) < self.rows:
            lines.append(move)
        return lines


# ─── Block-art renderer (universal fallback for truecolor terminals) ───

# Each terminal cell renders two stacked pixels via the ▀ glyph
# (upper-half-block): foreground colour = top pixel, background = bottom.
# `▄` is used when only the bottom half has content (top is transparent),
# and a plain space when both halves are transparent.
_UPPER = "▀"
_LOWER = "▄"
_TRANSPARENT_ALPHA = 80   # below this, treat pixel as transparent


def _ansi_truecolor_cell(top, bot) -> str:
    """Render one terminal cell (= 2 vertical pixels) as a colored block."""
    t_clear = top[3] < _TRANSPARENT_ALPHA
    b_clear = bot[3] < _TRANSPARENT_ALPHA
    if t_clear and b_clear:
        return " "
    if t_clear:
        return f"\x1b[38;2;{bot[0]};{bot[1]};{bot[2]}m{_LOWER}\x1b[0m"
    if b_clear:
        return f"\x1b[38;2;{top[0]};{top[1]};{top[2]}m{_UPPER}\x1b[0m"
    return (f"\x1b[38;2;{top[0]};{top[1]};{top[2]};"
            f"48;2;{bot[0]};{bot[1]};{bot[2]}m{_UPPER}\x1b[0m")


class BlockArtRenderer:
    """Clawd rendered as ANSI half-block art (one cell = 2 vertical px)."""

    def __init__(self, assets: Path, cols: int = 24, rows: int = 8):
        self.assets = assets
        self.cols = cols
        self.rows = rows
        self.indent_cols = cols + 2
        self.pixel_h = rows * 2
        self._cache: dict[str, list[str]] = {}  # state → image lines

    def preload(self, states=None):
        for st in states or list(_STATE_GIF):
            self._lines_for(st)

    def _lines_for(self, state: str) -> Optional[list[str]]:
        if state in self._cache:
            return self._cache[state]
        gif_name = _STATE_GIF.get(state)
        if not gif_name:
            return None
        pixels = _load_frame0_pixels(self.assets / gif_name, self.cols, self.pixel_h)
        if pixels is None:
            return None
        lines: list[str] = []
        for cy in range(self.rows):
            row_cells: list[str] = []
            for cx in range(self.cols):
                top = pixels[(2 * cy) * self.cols + cx]
                bot = pixels[(2 * cy + 1) * self.cols + cx]
                row_cells.append(_ansi_truecolor_cell(top, bot))
            lines.append("".join(row_cells))
        self._cache[state] = lines
        return lines

    def render_card(self, state: str, text_lines: list[str], gap: int = 2) -> list[str]:
        """Stack image (cell rows) beside text rows; pad whichever is shorter."""
        img_lines = self._lines_for(state) or [" " * self.cols] * self.rows
        blank = " " * self.cols
        height = max(self.rows, len(text_lines))
        out: list[str] = []
        for i in range(height):
            left = img_lines[i] if i < len(img_lines) else blank
            right = text_lines[i] if i < len(text_lines) else ""
            out.append(f"{left}{' ' * gap}{right}")
        return out


# ─── Pack renderer (sprite-pack JSON, bundled with the package) ────────

# Map agent-fleet states → pack-state vocabulary (pixel-buddy spec).
_PACK_STATE_MAP = {
    "idle":         "idle",
    "thinking":     "thinking",
    "building":     "tool_running",
    "typing":       "tool_running",
    "happy":        "done",
    "sleeping":     "idle",
    "juggling":     "subagent_running",
    "conducting":   "subagent_running",
    "error":        "tool_failure",
    "notification": "question",
    "sweeping":     "idle",
    "carrying":     "tool_running",
}


def _hex_rgb(hex_str: str) -> tuple[int, int, int]:
    h = hex_str.lstrip("#")
    return int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16)


def _pack_cell(top, bot) -> str:
    """Like _ansi_truecolor_cell but takes hex/None directly (no alpha)."""
    if top is None and bot is None:
        return " "
    if top is None:
        r, g, b = _hex_rgb(bot)
        return f"\x1b[38;2;{r};{g};{b}m{_LOWER}\x1b[0m"
    if bot is None:
        r, g, b = _hex_rgb(top)
        return f"\x1b[38;2;{r};{g};{b}m{_UPPER}\x1b[0m"
    rt, gt, bt = _hex_rgb(top)
    rb, gb, bb = _hex_rgb(bot)
    return (f"\x1b[38;2;{rt};{gt};{bt};48;2;{rb};{gb};{bb}m{_UPPER}\x1b[0m")


class PackRenderer:
    """Render a TeXmeijin-format sprite pack (pure data, no Pillow).

    Each sprite is a 2D palette-index grid; `renderMode: 'half-block'` means
    each pair of sprite rows collapses to one terminal cell row (▀ with
    fg=top-pixel, bg=bottom-pixel); `pixelWidth=N` repeats each pixel-cell
    N times horizontally so the character reads square in the terminal.
    Animation frames cycle on the wall clock at the per-state period
    declared in the pack.
    """

    def __init__(self, pack_path: Path):
        self._pack = json.loads(pack_path.read_text(encoding="utf-8"))
        sp = self._pack["sprite"]
        self.pixel_width = int(sp.get("pixelWidth", 1))
        sw = int(sp["width"])
        sh = int(sp["height"])
        self.cols = sw * self.pixel_width
        self.rows = sh // 2
        self.indent_cols = self.cols + 2
        self.palette: list = sp["palette"]  # idx 0 is None (transparent)
        self._states: dict = self._pack.get("states", {})
        self._timing: dict = self._pack.get("timing", {})
        # Pre-render every sprite once.
        self._sprite_lines: dict[str, list[str]] = {
            name: self._render_grid(grid)
            for name, grid in self._pack["sprites"].items()
        }

    def preload(self, states=None) -> None:
        pass  # already done in __init__

    def _render_grid(self, grid: list[list[int]]) -> list[str]:
        out_rows: list[str] = []
        sh = len(grid)
        sw = len(grid[0]) if grid else 0
        for cy in range(sh // 2):
            row_cells: list[str] = []
            top_row = grid[2 * cy]
            bot_row = grid[2 * cy + 1]
            for sx in range(sw):
                top = self.palette[top_row[sx]] if top_row[sx] else None
                bot = self.palette[bot_row[sx]] if bot_row[sx] else None
                row_cells.append(_pack_cell(top, bot) * self.pixel_width)
            out_rows.append("".join(row_cells))
        return out_rows

    def _pick_frame_name(self, pack_state: str) -> str:
        frames = self._states.get(pack_state) or self._states.get("idle") or ["idle_1"]
        if len(frames) == 1:
            return frames[0]
        period_key = f"{pack_state}FramePeriodMs"
        period = int(self._timing.get(period_key, 800))
        idx = int(time.time() * 1000 / period) % len(frames)
        return frames[idx]

    def render_card(self, state: str, text_lines: list[str], gap: int = 2) -> list[str]:
        pack_state = _PACK_STATE_MAP.get(state, "idle")
        sprite_name = self._pick_frame_name(pack_state)
        img_lines = self._sprite_lines.get(sprite_name) or [" " * self.cols] * self.rows
        blank = " " * self.cols
        height = max(self.rows, len(text_lines))
        out: list[str] = []
        for i in range(height):
            left = img_lines[i] if i < len(img_lines) else blank
            right = text_lines[i] if i < len(text_lines) else ""
            out.append(f"{left}{' ' * gap}{right}")
        return out


def bundled_pack_path() -> Optional[Path]:
    """Path to the bundled pack JSON, or None if it's missing."""
    custom = os.environ.get("AGENT_FLEET_CLAWD_PACK")
    p = Path(custom) if custom else (Path(__file__).parent / "packs" / "pixel-buddy.json")
    return p if p.is_file() else None


# ─── auto picker ───────────────────────────────────────────────────────

Renderer = "PackRenderer | KittyRenderer | BlockArtRenderer"


def maybe_renderer() -> "Optional[Renderer]":
    """Return a renderer ONLY when explicitly requested via AGENT_FLEET_CLAWD.

    Default is None — the board renders plain text cards with no character
    image. The renderer classes (Pack / Kitty / BlockArt) stay available for
    opt-in via env:

      AGENT_FLEET_CLAWD=pack       bundled sprite pack (must exist on disk)
      AGENT_FLEET_CLAWD=kitty      Kitty graphics, clawd-on-desk assets
      AGENT_FLEET_CLAWD=block      half-block ANSI, clawd-on-desk assets

    Defaulting off keeps the board uncluttered until a renderer the user
    actually likes ships.
    """
    forced = (os.environ.get("AGENT_FLEET_CLAWD") or "").lower()
    if forced in ("", "0", "off", "no", "false"):
        return None
    if forced == "pack":
        p = bundled_pack_path()
        return PackRenderer(p) if p else None
    a = assets_dir()
    if forced == "kitty":
        return KittyRenderer(a) if a else None
    if forced in ("block", "blockart", "block-art"):
        return BlockArtRenderer(a) if a else None
    return None
