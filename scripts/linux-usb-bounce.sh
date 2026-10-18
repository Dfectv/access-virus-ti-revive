#!/usr/bin/env bash
# linux-usb-bounce.sh - a short logical USB drop of the Virus TI on Linux, for reconnect tests (E175, E213).
#   sudo scripts/linux-usb-bounce.sh [--freeze PID] [GAP_S]
# Sets the Virus' sysfs "authorized" to 0 for GAP_S seconds (default 0.3) and back to 1: the kernel drops and
# re-enumerates the device like a cable pull, the Virus stays powered, nothing is sent to it. With --freeze PID the
# host process is stopped (SIGSTOP) before the drop and continued 4 s after it, so its worker cannot see the Virus
# absent - the PresenceWatch "bounced" case of a wake (E213). Needs root for the sysfs write.
set -euo pipefail
pid=
if [ "${1:-}" = --freeze ]; then
    pid=$2
    shift 2
fi
gap=${1:-0.3}
dev=
for d in /sys/bus/usb/devices/*; do
    if [ -f "$d/idVendor" ] && [ "$(cat "$d/idVendor")" = 133e ]; then
        dev=$d
        break
    fi
done
[ -n "$dev" ] || { echo "no Virus TI (USB vendor 133e) found" >&2; exit 1; }
echo "Virus at $dev, drop for $gap s${pid:+, host $pid frozen}"
# never leave the Virus unauthorized or the host stopped, also on an error or Ctrl+C
trap 'echo 1 > "$dev/authorized"; [ -z "$pid" ] || kill -CONT "$pid" 2>/dev/null || true' EXIT
[ -z "$pid" ] || kill -STOP "$pid"
echo 0 > "$dev/authorized"
sleep "$gap"
echo 1 > "$dev/authorized"
if [ -n "$pid" ]; then
    sleep 4 # the broker reopens the Virus meanwhile (~0.3-1 s)
    kill -CONT "$pid"
fi
echo "done $(date +%T.%3N)"
