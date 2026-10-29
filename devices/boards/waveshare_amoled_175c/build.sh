#!/bin/sh
# Builds the firmware for this board; with --upload, also flashes it.
# APP picks the display program from devices/apps (halo by default).
# Needs arduino-cli with the esp32 core and SensorLib, and secrets.h (copy
# secrets.example.h).
set -e
cd "$(dirname "$0")"
DEVICES=../..
APP="${APP:-halo}"
FQBN="esp32:esp32:esp32s3:CDCOnBoot=cdc,PSRAM=opi,FlashSize=32M,PartitionScheme=app5M_fat24M_32MB,FlashMode=qio"
PORT="${PORT:-/dev/cu.usbmodem1101}"

if [ ! -f secrets.h ]; then
  echo "Copy secrets.example.h to secrets.h and fill it in" >&2
  exit 1
fi
if [ ! -d "$DEVICES/apps/$APP" ]; then
  echo "No display program $DEVICES/apps/$APP" >&2
  exit 1
fi

# The shared core, the display program and the hanzi font go in the sketch's
# src/, which Arduino builds; side by side, so they include each other by name
rm -rf src
mkdir -p src/ui
cp "$DEVICES"/core/*.cpp "$DEVICES"/core/*.h "$DEVICES/apps/$APP"/*.cpp "$DEVICES/apps/$APP"/*.h src/ui/
python3 -I "$DEVICES/tools/embed.py" "$DEVICES/fonts/cjk10.bin" src/cjk10.h CJK10

# Arduino's bundled ctags is an Intel binary; the sketch declares everything
# before use, so the prototype scan it runs for is not needed
arduino-cli compile -b "$FQBN" \
  --build-property "tools.ctags.path=/usr/bin" --build-property "tools.ctags.cmd.path=/usr/bin/true" \
  --warnings default --output-dir build .

if [ "$1" = "--upload" ]; then
  arduino-cli upload -p "$PORT" -b "$FQBN" --input-dir build .
fi
