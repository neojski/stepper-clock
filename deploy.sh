#!/usr/bin/env bash
# Check, build and upload the firmware to the clock.
#
#   ./deploy.sh                 over WiFi (OTA), to 192.168.0.99
#   ./deploy.sh --ip 1.2.3.4    over WiFi, to another address
#   ./deploy.sh --usb [PORT]    over USB (default /dev/ttyUSB0). The clock then
#                               can't park the hand first: re-zero it after.
#   ./deploy.sh --skip-checks   don't run sim/test.sh first
set -euo pipefail

dir="$(cd "$(dirname "$0")" && pwd)"
cd "$dir"

ip=192.168.0.99
usb=""
checks=1
while [[ $# -gt 0 ]]; do
  case "$1" in
    --ip) ip="$2"; shift 2 ;;
    --usb)
      usb=/dev/ttyUSB0
      shift
      if [[ $# -gt 0 && "$1" != --* ]]; then
        usb="$1"
        shift
      fi
      ;;
    --skip-checks) checks=0; shift ;;
    *) echo "unknown option $1" >&2; exit 1 ;;
  esac
done

# As in .vscode/arduino.json
board="esp8266:esp8266:d1_mini:xtal=80,vt=flash,exception=disabled,stacksmash=disabled,ssl=all,mmu=3232,non32xfer=fast,eesz=4M1M,ip=lm2f,dbg=Disabled,lvl=None____,wipe=all,baud=115200"

if [[ $checks == 1 ]]; then
  echo "== Checks"
  sim/test.sh
  echo
fi

echo "== Build ($(git describe --always --dirty 2> /dev/null || echo "not in git"))"
mkdir -p build
if ! arduino-cli compile -b "$board" --warnings default --output-dir build . > build/compile.log 2>&1; then
  cat build/compile.log
  echo "build failed" >&2
  exit 1
fi
# Just our own warnings and the size
grep -E "$dir/[^ ]*warning|IROM" build/compile.log || true
echo

if [[ -n "$usb" ]]; then
  echo "== Upload over USB ($usb)"
  arduino-cli upload -b "$board" -p "$usb" --input-dir build
  echo
  echo "Uploaded. The hand's zero is lost: re-zero it."
  exit
fi

echo "== Upload over WiFi ($ip)"
espota=$(ls ~/.arduino15/packages/esp8266/hardware/esp8266/*/tools/espota.py | tail -1)
# The clock parks the hand at 12 before taking the update
if ! python3 "$espota" -d -i "$ip" -f build/stepper-clock.ino.bin > build/upload.log 2>&1; then
  grep -v "Uploading" build/upload.log
  echo "upload failed" >&2
  exit 1
fi
grep -E "Result" build/upload.log || true

printf "Waiting for the clock to come back"
for _ in $(seq 20); do
  sleep 3
  printf "."
  if ping -c 1 -W 2 "$ip" > /dev/null 2>&1; then
    echo " back at $(date +%T)"
    exit
  fi
done
echo
echo "The clock hasn't answered for a minute. Is it on?" >&2
exit 1
