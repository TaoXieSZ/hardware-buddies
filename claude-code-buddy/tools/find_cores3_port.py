#!/usr/bin/env python3
"""按 MAC/序列号认出 CoreS3 的串口，避免插拔后端口号漂移导致烧错设备。

CoreS3 的 USB-JTAG 口每次换 USB 插孔就换名字（实测 usbmodem1401 →
21301 → 1101），而同一台 Mac 上还可能同时插着 Tab5 / cardputer。按端口号
盲烧迟早烧错设备，只信序列号。

用法:
    PORT=$(python3 tools/find_cores3_port.py) && pio run -t upload --upload-port $PORT
"""

import sys

CORES3_SERIAL = "44:1B:F6:E1:F5:04"   # 小咪那台 CoreS3
# 已知的其他设备，认错了要出事，列出来便于报错时提示
KNOWN_OTHERS = {
    "80:F1:B2": "Tab5",
    "50:78:7D": "cardputer",
}


def main() -> None:
    try:
        from serial.tools import list_ports
    except ImportError:
        sys.exit("需要 pyserial：用 PlatformIO 的 python 跑（~/.platformio/penv/bin/python）")

    others = []
    for p in list_ports.comports():
        hwid = p.hwid or ""
        if CORES3_SERIAL in hwid:
            print(p.device)
            return
        for prefix, name in KNOWN_OTHERS.items():
            if prefix in hwid:
                others.append(f"{p.device}({name})")

    msg = f"没找到 CoreS3（序列号 {CORES3_SERIAL}）。"
    if others:
        msg += f" 在线的其他设备：{', '.join(others)} —— 别往这些口烧。"
    else:
        msg += " 检查 USB 线是否插好、设备是否上电。"
    sys.exit(msg)


if __name__ == "__main__":
    main()
