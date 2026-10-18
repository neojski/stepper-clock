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
sim/run.sh --open --start 22:40:11 --minutes 3 --play drop@5
```

It prints the program changes and the checks, overall and per program (exit
code 2 if the motion goes over the motor's limits or the hand doesn't follow),
then with `--open` opens the viewer. A viewer page that's already open
reloads itself after each run, and `viewer.html#t=226&play` starts 226s in,
playing. Options are listed at the top of `sim/sim.cpp`.
Needs `g++`.

`sim/test.sh` checks that nothing's broken, and `./deploy.sh` runs it before
uploading. It runs the simulator through the playlist, the `next` command at awkward
moments and parking, with regular, irregular and very slow `loop()` timing, and
says which pass. Timing matters: a program that goes wrong when it's started
1ms late passes with a regular loop.

## Uploading

```
./deploy.sh
```

runs the checks (`sim/test.sh`), builds the firmware and uploads it over WiFi,
then waits for the clock to come back. `--skip-checks` skips the checks.

The clock runs ArduinoOTA (hostname `stepper-clock`, no password) at
`192.168.0.99`, reserved in the router; `--ip` uploads to another address.
Before taking the update the clock parks the hand at 12 o'clock so it keeps
its zero across the reboot (see below).

Over USB, needed for the first upload or if the firmware is broken so OTA
doesn't work:

```
./deploy.sh --usb            # or --usb /dev/ttyUSB1
```

Board settings are in `deploy.sh` (and `.vscode/arduino.json`). WiFi credentials
go in the gitignored `credentials.c` (declared in `credentials.h`). Needs
`arduino-cli` with the esp8266 core and the ezTime library.

## The hand's zero position

There is no position sensor. On every boot the clock assumes the hand is at 12
o'clock. OTA updates park the hand at 12 first, but anything else that
reboots the board loses the zero and the hand has to be re-zeroed:

- USB uploads
- opening the serial port (it resets the board)
- power cuts
- crashes

## Programs

In the order they play, 9 minutes in all:

- `seconds`: ticking seconds hand. Also plays for a while between some of the
  others
- `drop`: the hand is let go at 3 o'clock like a pendulum held out level and
  swings through 6, each swing smaller, coming to rest at 6 by half past the
  minute
- `ratchet`: like a finger pushing the hand round against a ratchet: it creeps
  forward against the resistance, sticks, then pops over the tooth and jumps
  ahead, 2 seconds every 2s
- `hours`: hour hand (12h per turn), creeping one microstep at a time
- `wiper`: from the hour the hand sweeps over to the second like a windscreen
  wiper, stops there a moment and sweeps back, every 6s
- `smoothSeconds`: continuously sweeping seconds hand
- `flick`: the hand is drawn back as if against a spring, then let go: it
  whips forward past the right second and wobbles back onto it, every 4s
- `sneeze`: ah... ah... AH... the hand leans back in three growing gasps, then
  CHOO: it lurches forward past the right second and shudders back onto it,
  every 6s
- `stuck`: the seconds hand can't get past where it is, ticking into the
  obstacle and bouncing back 4 times, then breaks through and catches up with
  a burst of quick ticks, every 10s
- `forwardAndBack`: every 2 seconds the hand ticks forward, forward, back,
  pause (R R L P): two steps forward, one step back, all sharp ticks. It's the
  same every time, and it keeps time: each forward tick moves it 1.5 seconds
- `pendulum`: swings ±18° around 6 o'clock, about every 2s
- `recoilTick`: a seconds hand that twitches back a little before each tick,
  like the hand of an old pendulum clock
- `bounce`: the hand is let go at 3 o'clock like the drop, but 6 o'clock is a
  floor: it falls and bounces back up the same side, each bounce lower, until
  it rests at 6

Each program gives the hand's angle and speed for the current time. They play
in the order of `playlist[]` in `schedule.cpp`, each for at least a set
duration. Most wait for a particular moment so they start exactly where the
hand already is, usually at rest:

- the drop starts at :15.25, once the seconds hand has ticked to 3 o'clock,
  and is at rest at 6 by :29.25
- the ratchet takes over from it at :30.25, where the seconds hand would be
  too. It's where the seconds hand is every 2s (the flick every 4s, the sneeze
  every 6s), so they start and end at one of those moments
- the wiper is at rest at the hour every 6s, so it takes over from `hours`
  and ends there
- stuck starts once the seconds hand is at rest and ends as the seconds hand
- `forwardAndBack` is where the seconds hand is at the start of each 2s phrase,
  so it starts and ends at any even second
- the pendulum takes over from the seconds hand at 198°, the end of its swing,
  and swings back at :34 when the next tick is due. It ends at the other end
  of its swing at :27.25, where the seconds hand is then
- `recoilTick` is at rest where and when the seconds hand is, so it can stand
  in for it
- the bounce starts and ends like the drop: at :15.25 at 3 o'clock, and at
  rest at 6 before :30.25

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
- `status`: the clock replies with a line saying how it's doing: the program,
  the time, uptime, free memory, WiFi strength, whether the time is synced,
  where it believes the hand is, why it last reset (with the exception if it
  crashed) and the last command it received. `./status.sh` asks it, and
  `./deploy.sh` does after an upload.
