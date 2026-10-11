# stepper-clock

A single-hand clock driven by a stepper motor (TMC2208 driver) on a Wemos D1
mini (ESP8266).

## Code layout

- `stepper-clock.ino`: what's specific to the hardware: WiFi, OTA updates, NTP
  time, the UDP API
- `platform.h`, `platform_arduino.cpp`: the little the rest needs from the
  hardware (time, random numbers, step pulses, logging)
- `animation.h`, `anim_*.cpp`: the animations, one per file
- `schedule.h`, `schedule.cpp`: the playlist and crossfades
- `motor.h`, `motor.cpp`: making the motor follow the animations

Apart from `stepper-clock.ino` and `platform_arduino.cpp` the code doesn't use
Arduino, so it can also be compiled on a computer, e.g. for a simulator.

To add an animation: write it in a new `anim_*.cpp`, declare it in
`animation.h` and add it to `playlist[]` in `schedule.cpp`.

## Simulator

`sim/` runs the same animation, schedule and motor code on a computer against
a fake clock, checks the hand's speed and acceleration against the motor's
limits, and shows the run on a clock face in `sim/viewer.html`:

```
sim/run.sh --start 22:40:11 --minutes 3 --play drop@5
```

It prints the program changes and the checks, overall and per program (exit
code 2 if the motion goes over the motor's limits or the hand doesn't follow),
then opens the viewer. Options are listed at the top
of `sim/sim.cpp`; `--no-open` skips the viewer. Needs `g++`.

## Uploading

Board settings are in `.vscode/arduino.json`. WiFi credentials go in the
gitignored `credentials.c` (declared in `credentials.h`).

### Over WiFi (OTA)

The clock runs ArduinoOTA (hostname `stepper-clock`, no password) at
`192.168.0.99` (reserved in the router). Compile, then:

```
arduino-cli compile -b esp8266:esp8266:d1_mini --output-dir build .
python3 ~/.arduino15/packages/esp8266/hardware/esp8266/3.1.2/tools/espota.py \
  -i 192.168.0.99 -f build/stepper-clock.ino.bin
```

Before flashing, the clock parks the hand at 12 o'clock so it keeps its zero
across the reboot (see below).

### Over USB

Needed for the first flash, or if the firmware is broken so OTA doesn't work:

```
arduino-cli compile --upload -p /dev/ttyUSB0 -b esp8266:esp8266:d1_mini .
```

## The hand's zero position

There is no position sensor. On every boot the clock assumes the hand is at 12
o'clock. OTA updates park the hand at 12 first, but anything else that
reboots the board loses the zero and the hand has to be re-zeroed:

- USB uploads
- opening the serial port (it resets the board)
- power cuts
- crashes

## Programs

In the order they play, about 7 minutes in all:

- `seconds`: ticking seconds hand. Also plays for a while between some of the
  others
- `drop`: the hand is let go at 3 o'clock like a pendulum held out level and
  swings through 6, each swing smaller, coming to rest at 6 by half past the
  minute
- `ratchet`: like a finger pushing the hand round against a ratchet: it creeps
  forward against the resistance, sticks, then pops over the tooth and jumps
  ahead, 2 seconds every 2s
- `hours`: hour hand (12h per turn), creeping one microstep at a time
- `smoothSeconds`: continuously sweeping seconds hand
- `flick`: the hand is drawn back as if against a spring, then let go: it
  whips forward past the right second and wobbles back onto it, every 4s
- `stuck`: the seconds hand can't get past where it is, ticking into the
  obstacle and bouncing back 4 times, then breaks through and catches up with
  a burst of quick ticks, every 10s
- `forwardAndBack`: the seconds hand ticks every 0.5s in short random groups,
  mostly forward with some steps back, each followed by a pause (e.g.
  `R R L P`), staying within about 2s of the right time
- `pendulum`: swings ±18° around 6 o'clock, about every 2s

Each program gives the hand's angle and speed for the current time. They play
in the order of `playlist[]` in `schedule.cpp`, each for at least a set
duration. Most wait for a particular moment so they start exactly where the
hand already is, usually at rest:

- the drop starts at :15.25, once the seconds hand has ticked to 3 o'clock,
  and is at rest at 6 by :29.25
- the ratchet takes over from it at :30.25, where the seconds hand would be
  too. It's where the seconds hand is every 2s (the flick every 4s), so they
  start and end at one of those moments
- stuck starts once the seconds hand is at rest and ends as the seconds hand
- `forwardAndBack` starts on a whole second, like a tick of the seconds hand
- the pendulum takes over from the seconds hand at 198°, the end of its swing,
  and swings back at :34 when the next tick is due. It ends at the other end
  of its swing at :27.25, where the seconds hand is then

Otherwise switching crossfades between the two programs, so the hand never
jumps. That's also what happens after a `play` command, until the playlist
reaches a program that crossfades in anyway.

The motor follows the program's speed plus a correction toward its position,
with the acceleration limited, so it doesn't drift or jerk. Motion has to stay
within about 360°/s and 720°/s² or the motor can miss steps.

Time is synced with NTP by ezTime, in the Europe/London time zone with BST
changes handled automatically.

## UDP API (port 8888)

- `next`: crossfade to the next program in the playlist now and continue from
  there. The stepper-clock button in Home Assistant sends this.
- `play <name>`: the same, to that program (e.g. `play drop`, names as in
  `playlist[]`). The drop glides to 3 o'clock first and then falls.

  ```
  echo "play drop" | nc -u -w1 192.168.0.99 8888
  ```
- `microsteps0` to `microsteps3`: set microstepping to 1/2, 1/4, 1/8 or 1/16
  (the default)
