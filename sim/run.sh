#!/usr/bin/env bash
# Build and run the simulator, then open the recording in viewer.html.
# Options go to the simulator (see sim.cpp), plus --no-open to skip the
# viewer. E.g.:
#
#   sim/run.sh --start 22:40:11 --minutes 3 --play drop@5
set -euo pipefail

dir="$(cd "$(dirname "$0")" && pwd)"
open=1
args=()
for arg in "$@"; do
  if [[ "$arg" == --no-open ]]; then
    open=0
  else
    args+=("$arg")
  fi
done

mkdir -p "$dir/build"
g++ -std=gnu++17 -O2 -Wall -I"$dir/.." -o "$dir/build/sim" \
  "$dir/sim.cpp" "$dir"/../anim_*.cpp "$dir/../schedule.cpp" "$dir/../motor.cpp"

status=0
"$dir/build/sim" --out "$dir/build/trace.js" "${args[@]}" || status=$?

if [[ $open == 1 ]]; then
  echo
  echo "Viewer: file://$dir/viewer.html"
  xdg-open "$dir/viewer.html" >/dev/null 2>&1 || true
fi
exit $status
