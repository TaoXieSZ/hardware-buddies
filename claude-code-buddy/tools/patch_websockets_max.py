"""pre 构建补丁：抬高 arduinoWebSockets 的单帧上限。

WEBSOCKETS_MAX_DATA_SIZE 在 WebSockets.h 里是无 #ifndef 保护的硬 #define
（上游 2.7.3 与 master 均如此），-D 覆盖不了；而 DashScope Realtime 的
response.audio.delta 文本帧实测最大 ~25KB（base64），超 15KB 默认值会被
clientDisconnect(1009) 直接断链。这里在每次构建前把 libdeps 里的 15KB 改成
48KB，幂等，clean 后重新拉库也会自愈。见 openspec change cores3-voice-assistant。
"""

Import("env")  # noqa: F821  (PlatformIO SCons context)
import pathlib

OLD = "#define WEBSOCKETS_MAX_DATA_SIZE (15 * 1024)"
NEW = "#define WEBSOCKETS_MAX_DATA_SIZE (48 * 1024)"

hdr = pathlib.Path(env["PROJECT_LIBDEPS_DIR"], env["PIOENV"], "WebSockets", "src", "WebSockets.h")
if hdr.exists():
    text = hdr.read_text()
    if OLD in text:
        hdr.write_text(text.replace(OLD, NEW))
        print(f"[patch_websockets_max] {hdr}: 15KB -> 48KB")
    elif NEW in text:
        print("[patch_websockets_max] already patched")
    else:
        print("[patch_websockets_max] WARNING: pattern not found — lib layout changed?")
