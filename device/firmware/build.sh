#!/bin/sh
# Builds the display firmware; with --upload, also flashes the board.
# Needs arduino-cli with the esp32 core and SensorLib, and
# agentsmon_display/secrets.h (copy secrets.example.h).
set -e
cd "$(dirname "$0")"
SKETCH=agentsmon_display
FQBN="esp32:esp32:esp32s3:CDCOnBoot=cdc,PSRAM=opi,FlashSize=32M,PartitionScheme=app5M_fat24M_32MB,FlashMode=qio"
PORT="${PORT:-/dev/cu.usbmodem1101}"

if [ ! -f "$SKETCH/secrets.h" ]; then
  echo "Copy $SKETCH/secrets.example.h to $SKETCH/secrets.h and fill it in" >&2
  exit 1
fi

# The UI core and the hanzi font go in the sketch's src/, which Arduino builds
rm -rf "$SKETCH/src"
mkdir -p "$SKETCH/src/ui"
cp ../ui/*.cpp ../ui/*.h "$SKETCH/src/ui/"
python3 -I embed.py ../fonts/cjk10.bin "$SKETCH/src/cjk10.h" CJK10

# Arduino's bundled ctags is an Intel binary; the sketch declares everything
# before use, so the prototype scan it runs for is not needed
arduino-cli compile -b "$FQBN" \
  --build-property "tools.ctags.path=/usr/bin" --build-property "tools.ctags.cmd.path=/usr/bin/true" \
  --warnings default --output-dir build "$SKETCH"

if [ "$1" = "--upload" ]; then
  arduino-cli upload -p "$PORT" -b "$FQBN" --input-dir build "$SKETCH"
fi
