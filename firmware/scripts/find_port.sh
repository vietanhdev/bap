# Sourced by the flash scripts: picks the Watcher's ESP32-S3 serial port unless PORT is set.
# The Watcher's CH342 shows up as two ports; interface 02 is the ESP32-S3.
if [ -z "${PORT:-}" ]; then
  mapfile -t _ports < <(ls /dev/serial/by-id/usb-1a86_USB_Dual_Serial_*-if02 2>/dev/null || true)
  if [ "${#_ports[@]}" -eq 0 ]; then
    echo "No SenseCAP Watcher found on USB. Plug it in or set PORT=/dev/ttyACM0" >&2
    exit 1
  elif [ "${#_ports[@]}" -gt 1 ]; then
    echo "Several Watchers are connected; choose one with PORT=...:" >&2
    printf '  %s\n' "${_ports[@]}" >&2
    exit 1
  fi
  PORT="${_ports[0]}"
fi
echo "Using $PORT" >&2
