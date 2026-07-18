#!/bin/sh
# 用 macOS 自带 say 生成 16kHz/mono/16bit 测试提问 wav（不用麦克风）。
# 用法: ./make_test_wav.sh "你好呀，你是谁？" q1.wav
set -e
TEXT="${1:?用法: $0 \"文本\" 输出.wav}"
OUT="${2:?用法: $0 \"文本\" 输出.wav}"
TMP="$(mktemp -t say).aiff"
# 优先中文音色 Tingting；没装则用系统默认
say -v Tingting -o "$TMP" "$TEXT" 2>/dev/null || say -o "$TMP" "$TEXT"
afconvert -f WAVE -d LEI16@16000 -c 1 "$TMP" "$OUT"
rm -f "$TMP"
echo "生成 $OUT ($(du -h "$OUT" | cut -f1))"
