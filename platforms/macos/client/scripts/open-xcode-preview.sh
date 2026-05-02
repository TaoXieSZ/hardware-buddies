#!/usr/bin/env bash
# 用 Xcode.app 打开统一引导视图与 Package，便于 Canvas 预览（需本机已安装 Xcode）。
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
export DEVELOPER_DIR="${DEVELOPER_DIR:-/Applications/Xcode.app/Contents/Developer}"
if [[ ! -d "$DEVELOPER_DIR" ]]; then
  echo "未找到 Xcode：$DEVELOPER_DIR" >&2
  exit 1
fi
xed "$ROOT/Package.swift"
xed "$ROOT/Sources/AhaKeyConfigUI/UnifiedTypelessOnboardingView.swift"
osascript -e 'tell application "Xcode" to activate' >/dev/null 2>&1 || true
echo "已在 Xcode 中打开。"
echo "方式 A：Scheme 选 AhaKeyConfigUI → 按 ⌥⌘↩ 打开 Canvas。"
echo "方式 B：Scheme 选 AhaKeyConfig → 运行（⌘R）→ 菜单「调试」→「预览统一引导…」（或 ⌥⌘P），不依赖 Canvas。"
