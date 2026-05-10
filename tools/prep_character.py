#!/usr/bin/env python3
"""
Prep a character pack: downscale GIFs to 96px with a CONSISTENT crop
across all states, so the character is the same size in every animation.
Writes to characters/<name>/ ready to drag onto the Hardware Buddy window.

Usage:
  python3 tools/prep_character.py <character-dir-or-zip>
"""
import json, sys, shutil, tempfile, zipfile
from pathlib import Path
from PIL import Image, ImageSequence

TARGET_W = 120   # screen is 135 wide on Plus2; ~7px margin each side
REF_W    = 1000   # normalize to this before computing the cross-state bbox
PROJECT  = Path(__file__).resolve().parent.parent
OUT_ROOT = PROJECT / "characters"


def _load_normalized(src_path: Path) -> tuple[list[Image.Image], list[int]]:
    """All frames at REF_W width, RGBA, with durations."""
    im = Image.open(src_path)
    frames, durations = [], []
    for f in ImageSequence.Iterator(im):
        durations.append(f.info.get("duration", 100))
        rgba = f.convert("RGBA").copy()
        scale = REF_W / rgba.width
        frames.append(rgba.resize((REF_W, round(rgba.height * scale)), Image.LANCZOS))
    return frames, durations


def _union(a, b):
    if a is None: return b
    if b is None: return a
    return (min(a[0], b[0]), min(a[1], b[1]), max(a[2], b[2]), max(a[3], b[3]))


def _save_state(frames, durations, dst: Path, bbox, bg_rgb):
    out = []
    for f in frames:
        cropped = f.crop(bbox)
        w, h = cropped.size
        new_h = max(1, round(h * TARGET_W / w))
        resized = cropped.resize((TARGET_W, new_h), Image.LANCZOS)
        flat = Image.new("RGB", resized.size, bg_rgb)
        flat.paste(resized, mask=resized.split()[-1])
        out.append(flat.convert("P", palette=Image.ADAPTIVE, colors=64))
    out[0].save(
        dst, save_all=True, append_images=out[1:],
        duration=durations, loop=0, optimize=False, disposal=1,
    )
    return dst.stat().st_size


def install(src: Path) -> None:
    if src.suffix == ".zip":
        tmp = Path(tempfile.mkdtemp())
        with zipfile.ZipFile(src) as z:
            z.extractall(tmp)
        found = list(tmp.rglob("manifest.json"))
        if not found:
            sys.exit("no manifest.json in zip")
        src = found[0].parent

    manifest = json.loads((src / "manifest.json").read_text())
    name = manifest["name"]
    bg_hex = manifest.get("colors", {}).get("bg", "#000000").lstrip("#")
    bg_rgb = tuple(int(bg_hex[i:i+2], 16) for i in (0, 2, 4))

    # Pass 1: load each state and compute a PER-STATE bbox (was: one global
    # bbox). Per-state means each state's character fills its own canvas
    # instead of being padded out to match the widest pose across all states.
    loaded_by_state = {}   # state -> list[(out_name, frames, durations, src_bytes)]
    state_bbox = {}        # state -> bbox
    for state, cfg in manifest["states"].items():
        entries = cfg if isinstance(cfg, list) else [cfg]
        for i, entry in enumerate(entries):
            gif_src = src / entry
            if not gif_src.exists():
                print(f"  skip {state}[{i}]: {entry} not found")
                continue
            frames, durations = _load_normalized(gif_src)
            out_name = f"{state}_{i}.gif" if len(entries) > 1 else f"{state}.gif"
            loaded_by_state.setdefault(state, []).append(
                (out_name, frames, durations, gif_src.stat().st_size))
            for f in frames:
                state_bbox[state] = _union(state_bbox.get(state), f.getbbox())

    print()
    for s, bb in state_bbox.items():
        cw, ch = bb[2] - bb[0], bb[3] - bb[1]
        out_h = round(ch * TARGET_W / cw)
        print(f"  {s:10s} bbox {bb} -> {TARGET_W}x{out_h}")
    print()

    # Pass 2: write — each state crops to its OWN bbox.
    out = OUT_ROOT / name
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)

    device_states, total = {}, 0
    for state, items in loaded_by_state.items():
        bb = state_bbox[state]
        for out_name, frames, durations, src_bytes in items:
            dst = out / out_name
            after = _save_state(frames, durations, dst, bb, bg_rgb)
            total += after
            device_states.setdefault(state, []).append(out_name)
            print(f"  {out_name:14s} {src_bytes:>10,}b -> {after:>7,}b  ({len(frames)} frames)")
    # Collapse single-entry lists back to strings for the common case
    device_states = {k: (v[0] if len(v) == 1 else v) for k, v in device_states.items()}

    (out / "manifest.json").write_text(json.dumps({
        "name": name,
        "colors": manifest.get("colors", {}),
        "states": device_states,
    }, indent=2))

    cap_kb = 1800
    print(f"\nwrote {name}: {total:,} bytes -> {out}")
    if total > cap_kb * 1024:
        print(f"  warning: over {cap_kb}KB — desktop install will reject it")
        if not shutil.which("gifsicle"):
            hint = {
                "darwin": "brew install gifsicle",
                "win32":  "winget install LCDF.Gifsicle",
            }.get(sys.platform, "apt install gifsicle")
            print(f"  gifsicle not found: {hint}")
        gifs = " ".join(f'"{g}"' for g in out.glob("*.gif"))
        print(f"  shrink: gifsicle --batch --lossy=80 -O3 --colors 64 {gifs}")
    print("next: drag that folder onto the Hardware Buddy window")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    install(Path(sys.argv[1]))
