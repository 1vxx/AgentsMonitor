# Devices

Hardware displays that show the daemon's summary (`GET /v1/summary`, the
`summary` WebSocket message). A firmware image is one **display program** built
for one **board**; the review page runs the same display program in a browser.

```
devices/
├── core/     shared by every display program, no platform underneath
│             ui.h is what a display program implements; model (the summary),
│             events (waits, finished turns and reminders read off it),
│             gfx and text (drawing), synth (chimes), spring (motion)
├── apps/     display programs, each implementing core/ui.h
│   ├── halo/     the mood orb, session list and detail, for the 466x466 round panel
│   └── pets/     each session a pixel animal on an LED dot board, acting out its state
├── boards/   one per board: its drivers and an Arduino sketch that runs a display program
│   └── waveshare_amoled_175c/   Waveshare ESP32-S3-Touch-AMOLED-1.75C
├── sim/      the review page: a display program in WASM, with scripted or live summaries
├── fonts/    the hanzi bitmap font and the script that cuts it
└── tools/    build helpers shared by boards
```

## Building

Firmware, for a board (`APP` picks the display program, `halo` by default):

```sh
devices/boards/waveshare_amoled_175c/build.sh            # compile
devices/boards/waveshare_amoled_175c/build.sh --upload   # compile and flash
APP=halo PORT=/dev/cu.usbmodem1101 devices/boards/waveshare_amoled_175c/build.sh --upload
```

Each board's `secrets.h` (Wi-Fi, daemon address and token) is copied from its
`secrets.example.h` and never committed.

Review page, then serve `devices/` and open it (the `device` entry in
`.claude/launch.json` does this on port 8466):

```sh
devices/sim/build.sh
python3 -m http.server 8466 --bind 127.0.0.1 --directory devices
```

## Adding a display program

Make `apps/<name>/` with a `ui.cpp` that implements `core/ui.h`, plus any files
of its own. Sources sit side by side with the core's when built, so include
core headers by name (`#include "gfx.h"`). Build it with `APP=<name>`.

## Adding a board

Make `boards/<name>/` as an Arduino sketch (`<name>.ino` inside), with its
drivers, a `secrets.example.h` and a `build.sh` that copies `core/` and the
chosen app into the sketch's `src/ui/` (see the 1.75C's). A board that is not
ESP32 or not Arduino keeps the same shape: its own build script, the core and
the app compiled alongside.

`core/gfx` draws the 466x466 round frame for now; a board with another screen
size needs that made a parameter first.
