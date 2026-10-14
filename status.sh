#!/usr/bin/env bash
# Ask the clock how it's doing, e.g.
#
#   program=flick time=21:30:05 uptime=0d02h03m heap=31200 rssi=-54 ntp=set
#   hand=123.4 reset="Power on" commands=9 last="next" (12s ago)
#
# Usage: ./status.sh [--ip ADDRESS] [--wait SECONDS]
# Exits non-zero if the clock doesn't answer within --wait (default 5).
set -euo pipefail

ip=192.168.0.99
wait=5
while [[ $# -gt 0 ]]; do
  case "$1" in
    --ip) ip="$2"; shift 2 ;;
    --wait) wait="$2"; shift 2 ;;
    *) echo "unknown option $1" >&2; exit 1 ;;
  esac
done

exec python3 - "$ip" "$wait" <<'EOF'
import socket, sys, time

ip, wait = sys.argv[1], float(sys.argv[2])
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.settimeout(1.5)
end = time.time() + wait
while True:
    # Resent: the clock only listens once it's finished starting up
    sock.sendto(b"status", (ip, 8888))
    try:
        data, _ = sock.recvfrom(512)
        print(data.decode(errors="replace").strip())
        sys.exit(0)
    except socket.timeout:
        if time.time() >= end:
            break
print("no reply from the clock at " + ip, file=sys.stderr)
sys.exit(1)
EOF
