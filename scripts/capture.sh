#!/usr/bin/env bash
# capture.sh NAME [SECONDS] - capture usbmon traffic of the bus the Virus TI (133e:0815) is on.
# Writes captures/NAME.pcapng plus captures/NAME.meta (bus/address, for tshark filtering:
#   usb.bus_id == BUS && usb.device_address == ADDR).
set -euo pipefail
NAME=${1:?usage: capture.sh NAME [SECONDS]}; SECS=${2:-15}
OUT=~/virus-ti-re/captures
LINE=$(lsusb -d 133e:0815 || true)
[ -n "$LINE" ] || { echo "Virus TI (133e:0815) not visible on host USB" >&2; exit 1; }
BUS=$(sed -E 's/^Bus 0*([0-9]+) Device 0*([0-9]+).*/\1/' <<<"$LINE")
ADDR=$(sed -E 's/^Bus 0*([0-9]+) Device 0*([0-9]+).*/\2/' <<<"$LINE")
DEV=/dev/usbmon$BUS
[ -r "$DEV" ] || { echo "$DEV missing or not readable (need: sudo modprobe usbmon + read access)" >&2; exit 1; }
[ -e "$OUT/$NAME.pcapng" ] && { echo "$OUT/$NAME.pcapng exists, pick another name" >&2; exit 1; }
printf 'name=%s\nstart=%s\nbus=%s\naddr=%s\nfilter=usb.bus_id == %s && usb.device_address == %s\n' \
  "$NAME" "$(date -Is)" "$BUS" "$ADDR" "$BUS" "$ADDR" > "$OUT/$NAME.meta"
echo "capturing usbmon$BUS (device addr $ADDR) for ${SECS}s -> $OUT/$NAME.pcapng"
dumpcap -q -i "usbmon$BUS" -a "duration:$SECS" -w "$OUT/$NAME.pcapng"
N=$(tshark -r "$OUT/$NAME.pcapng" -Y "usb.device_address == $ADDR" 2>/dev/null | wc -l)
echo "frames for device: $N" | tee -a "$OUT/$NAME.meta"
