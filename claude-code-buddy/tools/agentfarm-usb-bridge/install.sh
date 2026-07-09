#!/usr/bin/env bash
# agentfarm-usb-bridge installer (macOS)
#
# Sets up:
#   1. Python venv at ~/.agentfarm-usb-bridge/venv with pyserial
#   2. launchd agent at ~/Library/LaunchAgents/com.agentfarm-usb-bridge.plist
#      pointing at THIS monorepo copy of bridge.py.
#
# agentfarm-usb-bridge owns NO BLE device and uses no buddy_core — it is a
# standalone USB-CDC serial bridge that polls the Agent Farm trigger-cursor
# admin API on localhost and streams trigger firings to the Tab5 (ESP32-P4,
# no radio) over serial as newline-delimited JSON.
#
# The serial port is auto-detected at install time (first Espressif
# VID:PID=303A:* device) but can be overridden with --port. Re-run install
# after replugging if the port changes.
#
# Idempotent — re-run any time. Uninstall: ./install.sh uninstall

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
INSTALL_ROOT="${HOME}/.agentfarm-usb-bridge"
VENV="${INSTALL_ROOT}/venv"
LOG_DIR="${HOME}/Library/Logs"
PLIST_LABEL="com.agentfarm-usb-bridge"
PLIST_DST="${HOME}/Library/LaunchAgents/${PLIST_LABEL}.plist"

# ─── helpers ───────────────────────────────────────────────────────────
detect_serial_port() {
  # Tab5 = ESP32-P4 USB-Serial-JTAG, VID 0x303A, serial starts 80:F1:B2:…
  # (per AGENTS.md). Distinguish from the Cardputer-ADV which is also 303A
  # but serial starts 50:78:7D:… — match by serial prefix so install grabs
  # the Tab5 even when both devices are plugged in.
  local port
  port="$("${VENV}/bin/python3" -c "
from serial.tools import list_ports
for d in list_ports.comports():
    if d.vid == 0x303A and (d.serial or '').startswith('80:F1:B2'):
        print(d.device); break
" 2>/dev/null || true)"
  if [[ -z "${port}" ]]; then
    echo "(no Tab5 (serial 80:F1:B2:*) found — is it plugged in and powered on? Edit ${PLIST_DST} by hand)" >&2
    echo "/dev/cu.usbmodemPLACEHOLDER"
  else
    echo "${port}"
  fi
}

# ─── uninstall ─────────────────────────────────────────────────────────
uninstall() {
  echo "→ unloading launchd agent"
  launchctl bootout "gui/$(id -u)/${PLIST_LABEL}" 2>/dev/null || true
  rm -f "${PLIST_DST}"
  echo "✓ uninstalled. venv at ${VENV} left in place — rm -rf manually if you want."
}

if [[ "${1:-}" == "uninstall" ]]; then uninstall; exit 0; fi

# ─── 1. Python venv ────────────────────────────────────────────────────
mkdir -p "${INSTALL_ROOT}" "${LOG_DIR}"
if [[ ! -d "${VENV}" ]]; then
  echo "→ creating venv at ${VENV}"
  python3 -m venv "${VENV}"
fi
echo "→ installing pyserial into venv"
"${VENV}/bin/pip" install --quiet --upgrade pip pyserial

# ─── 2. serial port ────────────────────────────────────────────────────
SERIAL_PORT="$(detect_serial_port)"
echo "→ detected serial port: ${SERIAL_PORT}"

# ─── 3. launchd plist ──────────────────────────────────────────────────
echo "→ writing launchd plist to ${PLIST_DST}"
mkdir -p "$(dirname "${PLIST_DST}")"
sed \
  -e "s|__VENV_PYTHON__|${VENV}/bin/python3|g" \
  -e "s|__BRIDGE_PY__|${HERE}/bridge.py|g" \
  -e "s|__LOG_DIR__|${LOG_DIR}|g" \
  -e "s|__SERIAL_PORT__|${SERIAL_PORT}|g" \
  "${HERE}/com.agentfarm-usb-bridge.plist.template" > "${PLIST_DST}"

echo "→ (re)loading launchd agent"
launchctl bootout "gui/$(id -u)/${PLIST_LABEL}" 2>/dev/null || true
sleep 1
if ! launchctl bootstrap "gui/$(id -u)" "${PLIST_DST}"; then
  echo "  ! bootstrap failed; trying kickstart."
  launchctl kickstart -k "gui/$(id -u)/${PLIST_LABEL}" 2>/dev/null || \
    echo "  ! kickstart failed — bring it up manually:" \
         "launchctl bootstrap gui/\$(id -u) ${PLIST_DST}"
fi

echo "✓ installed. Logs: ${LOG_DIR}/agentfarm-usb-bridge.log"
echo "  ⚠ if the Tab5 was not plugged in, the port is a placeholder — re-run install with it connected."
