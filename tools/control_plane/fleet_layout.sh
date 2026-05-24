#!/usr/bin/env bash
# Lay out the voice control-plane in ONE cmux workspace:
#   left pane  = live fleet board   (terminal: board.py --watch)
#   right pane = voice secretary    (browser surface -> the Agora voice UI)
#
# Your coding-agent sessions stay as their own cmux workspaces (tabs) in the
# same window, so "one interface = voice agent + board + all sessions".
#
# cmux recipe (probed against cmux-socket, 2026-05-24):
#   new-workspace --command <cmd>          -> prints "OK workspace:N" (a ref, not JSON)
#   rpc workspace.list {}                  -> map that ref to the stable UUID
#   rpc surface.list {workspace_id}        -> find the board's terminal surface
#   rpc browser.open_split {surface_id,url}-> browser split BESIDE the board, same ws
#   (surface.create places by *current* focus, not workspace_id, so open_split — which
#    splits relative to a source surface — is the reliable way to co-locate them.)
#   rpc workspace.current {workspace_id}   -> focus the finished layout for the user
# Re-run to spin up a fresh layout; close panes with the cmux UI when done.
set -uo pipefail

CMUX="${CMUX:-/Applications/cmux.app/Contents/Resources/bin/cmux}"
VOICE_URL="${FLEET_VOICE_URL:-http://localhost:3000}"
TOOLS_DIR="$(cd "$(dirname "$0")/.." && pwd)"          # .../tools
BOARD_CMD="cd '$TOOLS_DIR' && python3 -m control_plane.board --watch"

[ -x "$CMUX" ] || { echo "cmux not found at $CMUX (set \$CMUX)"; exit 1; }

# 1. board workspace (terminal running the live board). Output is "OK workspace:N".
WS_OUT="$("$CMUX" new-workspace --name "fleet board" --command "$BOARD_CMD")" || {
  echo "new-workspace failed: $WS_OUT"; exit 1; }
WS_REF="$(printf '%s' "$WS_OUT" | grep -oE 'workspace:[0-9]+' | head -1)"

# 2. resolve ref -> stable UUID, then find the board's terminal surface in it.
WS_ID="$("$CMUX" rpc workspace.list '{}' 2>/dev/null | python3 -c "
import sys, json
ref = '$WS_REF'
ws = json.load(sys.stdin).get('workspaces', [])
print(next((w['id'] for w in ws if w.get('ref') == ref), ''))
" 2>/dev/null)"
SURF_ID="$("$CMUX" rpc surface.list "{\"workspace_id\":\"$WS_ID\"}" 2>/dev/null | python3 -c "
import sys, json
ss = json.load(sys.stdin).get('surfaces', [])
print(next((s['id'] for s in ss if s.get('type') == 'terminal'), ss[0]['id'] if ss else ''))
" 2>/dev/null)"

# 3. voice secretary as a browser split BESIDE the board (same workspace).
if [ -n "$SURF_ID" ]; then
  "$CMUX" rpc browser.open_split "{\"surface_id\":\"$SURF_ID\",\"url\":\"$VOICE_URL\"}" >/dev/null
else
  echo "warn: board surface not found; opening voice in the current workspace"
  "$CMUX" rpc surface.create "{\"type\":\"browser\",\"url\":\"$VOICE_URL\"}" >/dev/null
fi

# 4. focus the finished layout so the user lands on it.
[ -n "$WS_ID" ] && "$CMUX" rpc workspace.current "{\"workspace_id\":\"$WS_ID\"}" >/dev/null 2>&1

echo "Fleet layout ready (${WS_REF:-workspace ?}):"
echo "  - board : terminal, $TOOLS_DIR  ->  python -m control_plane.board --watch"
echo "  - voice : browser  ->  $VOICE_URL"
echo "Coding sessions remain separate cmux workspaces/tabs in this window."
