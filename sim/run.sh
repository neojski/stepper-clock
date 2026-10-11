#!/usr/bin/env bash
# Build and run the simulator, recording the run for viewer.html. An open
# viewer page reloads itself to show it; --open opens one. --build-only just
# builds. Other options go to the simulator (see sim.cpp). E.g.:
#
#   sim/run.sh --open --start 22:40:11 --minutes 3 --play drop@5
set -euo pipefail

dir="$(cd "$(dirname "$0")" && pwd)"
open=0
build_only=0
args=()
for arg in "$@"; do
  if [[ "$arg" == --open ]]; then
    open=1
  elif [[ "$arg" == --build-only ]]; then
    build_only=1
  elif [[ "$arg" == --no-open ]]; then
    : # the default now
  else
    args+=("$arg")
  fi
done

mkdir -p "$dir/build"
g++ -std=gnu++17 -O2 -Wall -I"$dir/.." -o "$dir/build/sim" \
  "$dir/sim.cpp" "$dir"/../anim_*.cpp "$dir/../schedule.cpp" "$dir/../motor.cpp"
if [[ $build_only == 1 ]]; then
  exit
fi

status=0
"$dir/build/sim" --out "$dir/build/trace.js" "${args[@]}" || status=$?
# Tells an open viewer there's a new recording
echo "window.TRACE_STAMP = \"$(date +%s%N)\";" > "$dir/build/stamp.js"

echo
echo "Viewer: file://$dir/viewer.html"
if [[ $open == 1 ]]; then
  xdg-open "$dir/viewer.html" >/dev/null 2>&1 || true
fi
exit $status
