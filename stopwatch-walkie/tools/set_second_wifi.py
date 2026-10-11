#!/usr/bin/env python3
"""Write a second Wi-Fi network (e.g. home) into the ignored include/walkie_config.h.

The password is read with getpass and never echoed or printed. Rebuild and flash afterwards.
"""

import getpass
import os
import re
from pathlib import Path

CONFIG = Path(__file__).resolve().parents[1] / "include" / "walkie_config.h"


def c_string(value: str) -> str:
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'


def main() -> None:
    ssid = input("家里 Wi-Fi 名称（SSID）: ").strip()
    password = getpass.getpass("家里 Wi-Fi 密码（输入时不显示）: ")
    if not ssid:
        raise SystemExit("没有输入名称，未修改")
    text = CONFIG.read_text()
    text = re.sub(r"^#define WIFI_(SSID|PASSWORD)_2 .*\n", "", text, flags=re.M)
    text = text.rstrip("\n") + f"\n#define WIFI_SSID_2 {c_string(ssid)}\n#define WIFI_PASSWORD_2 {c_string(password)}\n"
    CONFIG.write_text(text)
    os.chmod(CONFIG, 0o600)
    print(f"已写入第二个网络（名称 {len(ssid)} 个字符，密码 {len(password)} 个字符）。回到 Claude 说一声即可。")


if __name__ == "__main__":
    main()
