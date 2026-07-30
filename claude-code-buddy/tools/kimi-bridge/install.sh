#!/usr/bin/env bash
# kimi-bridge installer (macOS)
#
# Sets up:
#   1. Python venv at ~/.kimi-bridge/venv with bleak + pyserial + Quartz
#   2. launchd agent at ~/Library/LaunchAgents/com.kimi-bridge.plist
#   3. [[hooks]] blocks in ~/.kimi-code/config.toml that fire kimi_hook.js
#      for the Kimi Code lifecycle events (SessionStart, UserPromptSubmit,
#      PreToolUse, PostToolUse, PostToolUseFailure, Stop, StopFailure,
#      Interrupt, PermissionRequest, SessionEnd).
#
# kimi-bridge owns NO BLE device of its own — it pushes per-session
# snapshots to cc-bridge's socket (single-BLE-owner aggregation). So there
# is no stick to pair; cc-bridge must be running and own the cardputer's
# BLE link.
#
# Kimi's hooks live in ~/.kimi-code/config.toml as a TOML array
#   [[hooks]]
#   event = "SessionStart"
#   command = "\"node\" \"…/kimi_hook.js\""
#   timeout = 5
# — NOT JSON, so no jq merge. We only APPEND our blocks (idempotent via a
# grep for kimi_hook.js) and back up before every edit. config.toml contains
# API keys — we never print it; grep is used for existence checks only.
# Backups are named config.toml.kimi-bridge.bak.<epoch> to stay distinct
# from the directory's own automatic backup files.
#
# Kimi hooks are fail-open (a failing/absent hook never blocks the CLI) and
# there is no hook-trust step like Codex's — installed hooks run as-is.
#
# NOTE: the permission echo (device button → allow/deny) is deferred — this
# installer wires only the fire-and-forget display hook (kimi_hook.js). The
# PermissionRequest event is wired async too, so the device still SHOWS the
# waiting state; it just doesn't gate Kimi yet.
#
# Idempotent — re-run any time. Uninstall: ./install.sh uninstall

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
INSTALL_ROOT="${HOME}/.kimi-bridge"
VENV="${INSTALL_ROOT}/venv"
LOG_DIR="${HOME}/Library/Logs"
SOCKET_PATH="/tmp/kimi-bridge.sock"
PLIST_LABEL="com.kimi-bridge"
PLIST_DST="${HOME}/Library/LaunchAgents/${PLIST_LABEL}.plist"
CONFIG_TOML="${HOME}/.kimi-code/config.toml"

# Fire-and-forget Kimi hook events → kimi_hook.js. Kimi event names are
# already Claude-Code-shaped, so the daemon's apply_event consumes them
# directly (kimi_hook.js whitelists + maps PostToolUseFailure).
HOOK_EVENTS_ASYNC=(
  SessionStart
  UserPromptSubmit
  PreToolUse
  PostToolUse
  PostToolUseFailure
  Stop
  StopFailure
  Interrupt
  PermissionRequest
  SessionEnd
)
HOOK_BASENAME="kimi_hook.js"
ASYNC_HOOK_TIMEOUT_S=5

# ─── helpers ───────────────────────────────────────────────────────────
detect_node() {
  if command -v node >/dev/null 2>&1; then command -v node
  elif [[ -x /opt/homebrew/bin/node ]]; then echo "/opt/homebrew/bin/node"
  elif [[ -x /usr/local/bin/node ]]; then echo "/usr/local/bin/node"
  else echo "✗ node not found. brew install node and re-run." >&2; exit 1; fi
}

backup_config() {
  if [[ -f "${CONFIG_TOML}" ]]; then
    local bak="${CONFIG_TOML}.kimi-bridge.bak.$(date +%s)"
    cp "${CONFIG_TOML}" "${bak}"
    echo "→ backed up config.toml → $(basename "${bak}")"
  fi
}

# Remove every TOML section that references our hook script. Sections are
# delimited by any line starting with '[' (covers both [table] and [[array]]
# headers); only our [[hooks]] blocks contain kimi_hook.js, everything else
# is preserved byte-for-byte.
strip_our_hooks() {
  local tmp; tmp="$(mktemp)"
  awk '
    /^\[/ {
      if (buf !~ /kimi_hook\.js/) printf "%s", buf;
      buf = $0 ORS; next
    }
    { buf = buf $0 ORS }
    END { if (buf !~ /kimi_hook\.js/) printf "%s", buf }
  ' "${CONFIG_TOML}" > "${tmp}" && mv "${tmp}" "${CONFIG_TOML}"
}

# ─── uninstall ─────────────────────────────────────────────────────────
uninstall() {
  echo "→ unloading launchd agent"
  launchctl bootout "gui/$(id -u)/${PLIST_LABEL}" 2>/dev/null || true
  rm -f "${PLIST_DST}" "${SOCKET_PATH}"
  if [[ -f "${CONFIG_TOML}" ]] && grep -q "${HOOK_BASENAME}" "${CONFIG_TOML}"; then
    backup_config
    echo "→ stripping kimi-bridge [[hooks]] blocks from config.toml"
    strip_our_hooks
  fi
  echo "✓ uninstalled. venv at ${VENV} left in place — rm -rf manually if you want."
}

if [[ "${1:-}" == "uninstall" ]]; then uninstall; exit 0; fi

# ─── 1. Python venv ────────────────────────────────────────────────────
mkdir -p "${INSTALL_ROOT}" "${LOG_DIR}"
if [[ ! -d "${VENV}" ]]; then
  echo "→ creating venv at ${VENV}"
  python3 -m venv "${VENV}"
fi
echo "→ installing bleak + pyserial + pyobjc-framework-Quartz into venv"
"${VENV}/bin/pip" install --quiet --upgrade pip bleak pyserial pyobjc-framework-Quartz

# ─── 2. launchd plist ──────────────────────────────────────────────────
echo "→ writing launchd plist to ${PLIST_DST}"
mkdir -p "$(dirname "${PLIST_DST}")"
sed \
  -e "s|__VENV_PYTHON__|${VENV}/bin/python3|g" \
  -e "s|__BRIDGE_PY__|${HERE}/bridge.py|g" \
  -e "s|__LOG_DIR__|${LOG_DIR}|g" \
  -e "s|__SOCKET_PATH__|${SOCKET_PATH}|g" \
  "${HERE}/com.kimi-bridge.plist.template" > "${PLIST_DST}"

echo "→ (re)loading launchd agent"
launchctl bootout "gui/$(id -u)/${PLIST_LABEL}" 2>/dev/null || true
sleep 1
if ! launchctl bootstrap "gui/$(id -u)" "${PLIST_DST}"; then
  echo "  ! bootstrap failed; trying kickstart."
  launchctl kickstart -k "gui/$(id -u)/${PLIST_LABEL}" 2>/dev/null || \
    echo "  ! kickstart failed — bring it up manually:" \
         "launchctl bootstrap gui/\$(id -u) ${PLIST_DST}"
fi

# ─── 3. append [[hooks]] blocks to ~/.kimi-code/config.toml ────────────
NODE_BIN="$(detect_node)"
echo "→ using node: ${NODE_BIN}"
mkdir -p "$(dirname "${CONFIG_TOML}")"
[[ -f "${CONFIG_TOML}" ]] || touch "${CONFIG_TOML}"
chmod +x "${HERE}/${HOOK_BASENAME}" 2>/dev/null || true

if grep -q "${HOOK_BASENAME}" "${CONFIG_TOML}"; then
  echo "→ config.toml already references ${HOOK_BASENAME} — hooks left as-is"
else
  backup_config
  echo "→ appending ${#HOOK_EVENTS_ASYNC[@]} [[hooks]] blocks to config.toml"
  {
    for ev in "${HOOK_EVENTS_ASYNC[@]}"; do
      printf '[[hooks]]\n'
      printf '# kimi-bridge — fire %s (fire-and-forget, fail-open)\n' "${HOOK_BASENAME}"
      printf 'event = "%s"\n' "${ev}"
      printf 'command = "\\"%s\\" \\"%s/%s\\""\n' "${NODE_BIN}" "${HERE}" "${HOOK_BASENAME}"
      printf 'timeout = %d\n\n' "${ASYNC_HOOK_TIMEOUT_S}"
    done
  } >> "${CONFIG_TOML}"
  echo "→ wired async hooks: ${HOOK_EVENTS_ASYNC[*]}"
fi

cat <<EOF

✓ kimi-bridge installed.
  • daemon:  launchctl print gui/$(id -u)/${PLIST_LABEL}
  • log:     tail -f ${LOG_DIR}/kimi-bridge.log
  • socket:  ${SOCKET_PATH}  (pushes ext_sessions → cc-bridge)

Next:
  1. cc-bridge must be running (it owns the cardputer BLE link).
  2. Restart Kimi Code so it re-reads config.toml, then fire any action —
     kimi-bridge.log should show events within seconds.
  3. The cardputer session list should show the Kimi session with a purple
     "ki" marker (label from the cmux pane when one matches the session cwd,
     otherwise the directory basename).
EOF
