"""任务 1.2：探测哪个 wss URL × 模型 ID 组合能连通 Realtime API。

对每个候选组合建连、等首个服务端事件（应为 session.created），打印结论。
"""

import ssl
import sys

import websocket

from common import (FALLBACK_MODEL, MODEL, auth_headers, candidate_urls,
                    read_api_key)


def try_connect(url: str, key: str) -> tuple[bool, str]:
    try:
        ws = websocket.create_connection(
            url, header=auth_headers(key), timeout=10,
            sslopt={"cert_reqs": ssl.CERT_REQUIRED},
        )
    except Exception as e:  # noqa: BLE001 — 探测脚本，任何失败都只是一个结论
        return False, f"连接失败: {type(e).__name__}: {e}"
    try:
        first = ws.recv()
        return True, f"首事件: {first[:300]}"
    except Exception as e:  # noqa: BLE001
        return False, f"建连成功但收首事件失败: {type(e).__name__}: {e}"
    finally:
        ws.close()


def main() -> None:
    key = read_api_key()
    results = []
    for model in (MODEL, FALLBACK_MODEL):
        for url in candidate_urls(model):
            print(f"\n=== 试 {model}\n    {url}")
            ok, detail = try_connect(url, key)
            print(("✅ " if ok else "❌ ") + detail)
            results.append((ok, model, url))

    wins = [r for r in results if r[0]]
    print("\n----- 结论 -----")
    if not wins:
        print("全部失败。检查：key 是否有效、模型是否已在百炼控制台开通、"
              "是否需要设置 DASHSCOPE_WORKSPACE_ID 走 maas 形态 URL。")
        sys.exit(1)
    for _, model, url in wins:
        print(f"可用: {model} @ {url}")


if __name__ == "__main__":
    main()
