#!/usr/bin/env bash
# opencode-bridge installer (macOS)
#
# Sets up:
#   1. Python venv at ~/.opencode-bridge/venv with bleak + pyserial
#      (bleak is pulled in by buddy_core even though this bridge is no_ble
#      push-only — kept for import parity with sibling bridges).
#   2. launchd agent at ~/Library/LaunchAgents/com.opencode-bridge.plist
#      pointing at THIS monorepo copy of bridge.py.
#   3. Repoints the OpenCode plugin entry in ~/.config/opencode/opencode.json
#      at this dir's cardputer-permission.mjs (so OpenCode's
#      permission.asked events route to cc-bridge).
#
# opencode-bridge owns NO BLE device of its own — it pushes per-session
# snapshots (discovered from cmux session JSON + a tty fallback for manually
# launched opencode panes) to cc-bridge's socket as ext_sessions
# (agent:"opencode"). cc-bridge must be running and own the cardputer's BLE.
#
# Idempotent — re-run any time. Uninstall: ./install.sh uninstall

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
INSTALL_ROOT="${HOME}/.opencode-bridge"
VENV="${INSTALL_ROOT}/venv"
LOG_DIR="${HOME}/Library/Logs"
SOCKET_PATH="/tmp/opencode-bridge.sock"
PLIST_LABEL="com.opencode-bridge"
PLIST_DST="${HOME}/Library/LaunchAgents/${PLIST_LABEL}.plist"
OPENCODE_JSON="${HOME}/.config/opencode/opencode.json"
PLUGIN_MJS="${HERE}/cardputer-permission.mjs"

require_jq() {
  if ! command -v jq >/dev/null 2>&1; then
    echo "✗ jq not found. Install with: brew install jq"; exit 1
  fi
}

# ─── uninstall ─────────────────────────────────────────────────────────
uninstall() {
  echo "→ unloading launchd agent"
  launchctl bootout "gui/$(id -u)/${PLIST_LABEL}" 2>/dev/null || true
  rm -f "${PLIST_DST}" "${SOCKET_PATH}"
  echo "✓ uninstalled. venv at ${VENV} left in place — rm -rf manually if you want."
  echo "  (OpenCode plugin entry in ${OPENCODE_JSON} not removed — edit by hand if desired)"
}

if [[ "${1:-}" == "uninstall" ]]; then uninstall; exit 0; fi

# ─── 1. Python venv ────────────────────────────────────────────────────
mkdir -p "${INSTALL_ROOT}" "${LOG_DIR}"
if [[ ! -d "${VENV}" ]]; then
  echo "→ creating venv at ${VENV}"
  python3 -m venv "${VENV}"
fi
echo "→ installing bleak + pyserial into venv"
"${VENV}/bin/pip" install --quiet --upgrade pip bleak pyserial

# ─── 2. launchd plist ──────────────────────────────────────────────────
echo "→ writing launchd plist to ${PLIST_DST}"
mkdir -p "$(dirname "${PLIST_DST}")"
sed \
  -e "s|__VENV_PYTHON__|${VENV}/bin/python3|g" \
  -e "s|__BRIDGE_PY__|${HERE}/bridge.py|g" \
  -e "s|__LOG_DIR__|${LOG_DIR}|g" \
  -e "s|__SOCKET_PATH__|${SOCKET_PATH}|g" \
  "${HERE}/com.opencode-bridge.plist.template" > "${PLIST_DST}"

echo "→ (re)loading launchd agent"
launchctl bootout "gui/$(id -u)/${PLIST_LABEL}" 2>/dev/null || true
sleep 1
if ! launchctl bootstrap "gui/$(id -u)" "${PLIST_DST}"; then
  echo "  ! bootstrap failed; trying kickstart."
  launchctl kickstart -k "gui/$(id -u)/${PLIST_LABEL}" 2>/dev/null || \
    echo "  ! kickstart failed — bring it up manually:" \
         "launchctl bootstrap gui/\$(id -u) ${PLIST_DST}"
fi

# ─── 3. OpenCode plugin entry ──────────────────────────────────────────
# Repoint the cardputer-permission.mjs plugin at THIS monorepo copy. The
# opencode.json "plugin" array is a list of absolute paths; we replace any
# entry whose basename is cardputer-permission.mjs with ours, or append ours.
echo "→ repointing OpenCode plugin entry in ${OPENCODE_JSON}"
if [[ -f "${OPENCODE_JSON}" ]]; then
  require_jq
  tmp="$(mktemp)"
  jq --arg p "${PLUGIN_MJS}" '
    .plugin = (.plugin // [])
    | .plugin |= (map(select(. | endswith("cardputer-permission.mjs") | not)) + [$p])
  ' "${OPENCODE_JSON}" > "${tmp}" && mv "${tmp}" "${OPENCODE_JSON}"
  echo "  ✓ plugin array now includes ${PLUGIN_MJS}"
  echo "  ⚠ restart opencode for the plugin change to take effect"
else
  echo "  ! ${OPENCODE_JSON} not found — add \"${PLUGIN_MJS}\" to its \"plugin\" array by hand"
fi

echo "✓ installed. Logs: ${LOG_DIR}/opencode-bridge.log  Socket: ${SOCKET_PATH}"
