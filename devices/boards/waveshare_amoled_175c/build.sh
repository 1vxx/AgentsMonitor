#!/bin/sh
# Builds the firmware for this board; with --upload, also flashes it.
# THEME picks the theme from devices/themes (halo by default); one theme goes
# into each image. PORT is the board's serial port.
# --pack <file or name> instead writes a theme's data, such as a photo pack,
# into the flash's data partition, and nothing else: no build, the firmware
# stays. A name is a pack devices/tools/face_pack.py made under it, for THEME
# (her if not given).
# Needs arduino-cli with the esp32 core and SensorLib, and secrets.h (copy
# secrets.example.h).
set -e
# The pack: a file as given, from where the script was run, or a named one
PACK=""
PACKS_OF="${THEME:-her}"
if [ "$1" = "--pack" ] && [ -n "$2" ]; then
  NAMED="$(dirname "$0")/../../sim/build/$PACKS_OF-packs/$2.pack"
  if [ -f "$2" ]; then PACK="$(cd "$(dirname "$2")" && pwd)/$(basename "$2")"
  elif [ -f "$NAMED" ]; then PACK="$(cd "$(dirname "$NAMED")" && pwd)/$(basename "$NAMED")"
  fi
fi
cd "$(dirname "$0")"
DEVICES=../..
THEME="${THEME:-${APP:-halo}}"
FQBN="esp32:esp32:esp32s3:CDCOnBoot=cdc,PSRAM=opi,FlashSize=32M,PartitionScheme=app5M_fat24M_32MB,FlashMode=qio"
PORT="${PORT:-/dev/cu.usbmodem1101}"
# Where that partition scheme (the core's large_fat_32MB.csv) puts the data partition, and its size
DATA_AT=0x910000
DATA_SIZE=$((0x16E0000))

if [ "$1" = "--pack" ]; then
  if [ -z "$PACK" ]; then
    echo "usage: build.sh --pack <file or name>; the names there are:" >&2
    ls "$DEVICES/sim/build/$PACKS_OF-packs" 2>/dev/null | sed -n 's/^\(.*\)\.pack$/  \1/p' >&2
    exit 1
  fi
  if [ "$(wc -c < "$PACK")" -gt "$DATA_SIZE" ]; then
    echo "$2 is larger than the data partition ($DATA_SIZE bytes)" >&2
    exit 1
  fi
  ESPTOOL=$(ls -d "$(arduino-cli config get directories.data)"/packages/esp32/tools/esptool_py/*/esptool | tail -1)
  "$ESPTOOL" --chip esp32s3 --port "$PORT" --baud 921600 write-flash "$DATA_AT" "$PACK"
  exit 0
fi

if [ ! -f secrets.h ]; then
  echo "Copy secrets.example.h to secrets.h and fill it in" >&2
  exit 1
fi
if [ ! -d "$DEVICES/themes/$THEME" ]; then
  echo "No theme $THEME; there are: $(for d in "$DEVICES"/themes/*/; do basename "$d"; done | tr '\n' ' ')" >&2
  exit 1
fi

# The shared core, the theme and the hanzi font go in the sketch's src/,
# which Arduino builds; side by side, so they include each other by name
rm -rf src
mkdir -p src/ui
cp "$DEVICES"/core/*.cpp "$DEVICES"/core/*.h src/ui/
find "$DEVICES/themes/$THEME" -maxdepth 1 \( -name '*.cpp' -o -name '*.h' \) -exec cp {} src/ui/ \;
python3 -I "$DEVICES/tools/embed.py" "$DEVICES/fonts/cjk10.bin" src/cjk10.h CJK10

# Arduino's bundled ctags is an Intel binary; the sketch declares everything
# before use, so the prototype scan it runs for is not needed
arduino-cli compile -b "$FQBN" \
  --build-property "tools.ctags.path=/usr/bin" --build-property "tools.ctags.cmd.path=/usr/bin/true" \
  --warnings default --output-dir build .

if [ "$1" = "--upload" ]; then
  arduino-cli upload -p "$PORT" -b "$FQBN" --input-dir build .
fi
