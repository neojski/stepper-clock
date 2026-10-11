#!/usr/bin/env bash
# Checks worth running before every upload: runs the simulator through the
# playlist, the API commands at awkward moments and parking, with both a
# regular and a slow irregular loop(), and says which pass. Each run passes if
# the hand stays within the motor's limits and follows the programs (see
# sim.cpp). Exits non-zero if any fail.
set -uo pipefail

dir="$(cd "$(dirname "$0")" && pwd)"
sim="$dir/build/sim"
"$dir/run.sh" --build-only || exit 1

# A perfectly regular loop() hides timing bugs, e.g. a program going wrong if
# it's started 1ms late. So most checks use a slow, irregular one, and some a
# very slow one, with which nearly everything starts a ms or two late.
regular=(--loop-us 300)
irregular=(--loop-us 700 --loop-jitter-us 600)
very_slow=(--loop-us 2000 --loop-jitter-us 1500)

failed=0

check() {
  local name="$1"
  shift
  local out
  if out=$("$sim" "$@" 2>&1); then
    echo "ok    $name"
  else
    echo "FAIL  $name"
    echo "        sim/run.sh $*"
    echo "$out" | grep -E "Peak|Tracking|unknown|TIMED OUT" | sed 's/^/        /'
    failed=1
  fi
}

# "next" every $1 seconds from $2, $3 times: steps through the whole playlist,
# starting each entry at a different point of its cycle
nexts() {
  awk -v every="$1" -v from="$2" -v count="$3" \
    'BEGIN { for (i = 0; i < count; i++) printf "--play next@%.2f ", from + i * every }'
}

echo "Playlist"
check "two cycles, regular loop" --start 10:05:20 --minutes 17 "${regular[@]}"
check "two cycles, irregular loop" --start 10:05:20.777 --minutes 17 "${irregular[@]}"
check "two cycles, another start time" --start 16:41:52.303 --minutes 17 "${irregular[@]}"
check "across midnight" --start 23:52:10.5 --minutes 17 "${irregular[@]}"
check "five cycles, very slow loop" --start 10:05:20.777 --minutes 41 "${very_slow[@]}"
check "five cycles, very slow loop, another start time" --start 03:17:44.019 --minutes 41 "${very_slow[@]}"

echo "The next command"
# shellcheck disable=SC2046
check "every 13.1s" --start 10:10:05.123 --minutes 8 $(nexts 13.1 3.4 28) "${irregular[@]}"
# shellcheck disable=SC2046
check "every 7.3s" --start 08:30:41.9 --minutes 6 $(nexts 7.3 2.2 30) "${irregular[@]}"
# shellcheck disable=SC2046
check "every 2.9s, mid-crossfade" --start 21:07:33.61 --minutes 5 $(nexts 2.9 4.1 40) "${irregular[@]}"
# shellcheck disable=SC2046
check "mashed every 0.4s" --start 10:10:05.123 --minutes 3 $(nexts 0.4 3.4 40) "${regular[@]}"
# shellcheck disable=SC2046
check "every 13.1s, very slow loop" --start 14:22:09.44 --minutes 8 $(nexts 13.1 3.4 28) "${very_slow[@]}"

echo "Parking for an OTA update"
# At moments through the cycle. It passes if the hand ends exactly at 12, in
# under 2s. (Its speed and acceleration aren't checked here: it uses all the
# acceleration allowed, which measures as slightly over.)
park_failed=0
for loop in regular irregular; do
  declare -n loop_args=$loop
  for at in $(awk 'BEGIN { for (t = 0.5; t < 480; t += 23.7) printf "%.1f ", t }'); do
    out=$("$sim" --start 10:05:20 --minutes 9 --park "$at" "${loop_args[@]}" 2>&1)
    took=$(echo "$out" | awk -v at="$at" '/parked at 0\.0 deg/ { printf "%.2f", $1 - at }')
    if [[ -z "$took" ]] || awk -v t="$took" 'BEGIN { exit !(t >= 2) }'; then
      echo "FAIL  parking ${at}s after boot, $loop loop: ${took:-not parked at 12}"
      echo "        sim/run.sh --start 10:05:20 --minutes 9 --park $at ${loop_args[*]}"
      park_failed=1
      failed=1
    fi
  done
done
if [[ $park_failed == 0 ]]; then
  echo "ok    from 21 moments through the cycle, both loops"
fi

echo
if [[ $failed == 0 ]]; then
  echo "All passed"
else
  echo "Some FAILED"
fi
exit $failed
