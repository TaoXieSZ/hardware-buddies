"""构建期把控制面板网页 gzip 成 C 头文件，嵌进固件。

为什么嵌固件而不放 LittleFS：小咪自 cat_face 起已不依赖 LittleFS（无需 uploadfs），
保持"一次 upload 就能用"的属性。gzip 后约 6-8KB，flash 占用可忽略。

源文件 src/stackchan_voice/panel.html 改动后重新构建即自动更新头文件。
"""

Import("env")  # noqa: F821  (PlatformIO SCons context)

import gzip
import pathlib

SRC = pathlib.Path("src/stackchan_voice/panel.html")
DST = pathlib.Path("src/stackchan_voice/panel_html.h")


def emit():
    if not SRC.exists():
        print(f"[gen_panel_html] 缺少 {SRC}，跳过")
        return
    raw = SRC.read_bytes()
    # mtime=0 让输出可复现：源文件没变时头文件字节也不变，不会污染 git diff。
    gz = gzip.compress(raw, compresslevel=9, mtime=0)

    if DST.exists():
        old = DST.read_text(encoding="utf-8")
        if f"// len={len(gz)} raw={len(raw)}" in old:
            print(f"[gen_panel_html] 已是最新（{len(raw)} → {len(gz)} 字节）")
            return

    lines = [
        "// 自动生成，请勿手改 —— 改 src/stackchan_voice/panel.html 后重新构建。",
        "// 由 tools/gen_panel_html.py 生成（gzip -9）。",
        f"// len={len(gz)} raw={len(raw)}",
        "#pragma once",
        "#include <pgmspace.h>",
        "#include <stdint.h>",
        "",
        f"constexpr size_t PANEL_HTML_GZ_LEN = {len(gz)};",
        "const uint8_t PANEL_HTML_GZ[] PROGMEM = {",
    ]
    for i in range(0, len(gz), 16):
        chunk = ", ".join(f"0x{b:02x}" for b in gz[i:i + 16])
        lines.append(f"    {chunk},")
    lines.append("};")
    DST.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"[gen_panel_html] {SRC} → {DST}（{len(raw)} → {len(gz)} 字节）")


emit()
