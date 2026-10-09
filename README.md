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

The clock cycles through these, switching when the next program's position
matches the hand's current position (at least 30s apart):

- `pendulum`: swings ±18° around 6 o'clock with a 2s period
- `seconds`: ticking seconds hand
- `hours`: hour hand (12h per turn), creeping one microstep at a time
- `smoothSeconds`: continuously sweeping seconds hand

Time is synced with NTP by ezTime, in the Europe/London time zone with BST
changes handled automatically.

## UDP API (port 8888)

- `microsteps0` to `microsteps3`: set microstepping to 1/2, 1/4, 1/8 or 1/16
  (the default)
- anything else: switch program. The first packet switches to `hours`.
  Packets sent within a minute of the last one step to the next program.
  Automatic switching pauses for a minute after any packet.
