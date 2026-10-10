# stepper-clock

A single-hand clock driven by a stepper motor (TMC2208 driver) on a Wemos D1
mini (ESP8266).

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

- `seconds`: ticking seconds hand
- `drop`: the hand is let go at 3 o'clock like a pendulum held out level and
  swings through 6, each swing smaller, coming to rest at 6 by half past the
  minute
- `forwardAndBack`: the seconds hand ticks every 0.5s in short random groups,
  mostly forward with some steps back, each followed by a pause (e.g.
  `R R L P`), staying within about 2s of the right time
- `hours`: hour hand (12h per turn), creeping one microstep at a time
- `smoothSeconds`: continuously sweeping seconds hand
- `pendulum`: swings ±18° around 6 o'clock with a 2s period

Each program gives the hand's angle and speed for the current time. They play
in the order of `playlist[]` in `stepper-clock.ino`, each for a set duration.
Some wait for a particular moment in the minute so they start where the hand
already is:

- the drop starts at :15.25, once the seconds hand has ticked to 3 o'clock,
  and is at rest at 6 by :29.25, where `forwardAndBack` takes over at :31 like the
  seconds hand would
- the pendulum starts at :27, where it and the seconds hand are both at rest
  at 162°, and runs for whole minutes so it ends there too

Otherwise switching crossfades between the two programs over 2s, so the hand
never jumps.

The motor follows the program's speed plus a correction toward its position,
with the acceleration limited, so it doesn't drift or jerk. Motion has to stay
within about 360°/s and 720°/s² or the motor can miss steps.

Time is synced with NTP by ezTime, in the Europe/London time zone with BST
changes handled automatically.

## UDP API (port 8888)

- `play <name>`: crossfade to that program now (e.g. `play drop`, names as
  in `playlist[]`) and continue the playlist from there. The drop glides to 3
  o'clock first and then falls.

  ```
  echo "play drop" | nc -u -w1 192.168.0.99 8888
  ```
- `microsteps0` to `microsteps3`: set microstepping to 1/2, 1/4, 1/8 or 1/16
  (the default)
- anything else: switch program, with a crossfade. The first packet switches
  to `hours`. Packets sent within a minute of the last one step to the next
  program. Automatic switching pauses for a minute after any packet.
