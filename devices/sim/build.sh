#!/bin/sh
# Builds every theme in devices/themes for the review page and snap.mjs, as
# build/<theme>.js and build/<theme>.wasm, and lists them, with what each
# theme.json says about it, in build/themes.json. THEME=<name> builds just
# that one (and lists them all).
# Needs Emscripten (brew install emscripten) and Python 3.
set -e
cd "$(dirname "$0")"
mkdir -p build

# The themes are the folders in devices/themes
ALL=$(for d in ../themes/*/; do basename "$d"; done)
if [ -n "${THEME:-${APP:-}}" ]; then
  THEMES="${THEME:-$APP}"
  if [ ! -d "../themes/$THEMES" ]; then
    echo "No theme $THEMES; there are: $(echo $ALL)" >&2
    exit 1
  fi
else
  THEMES=$ALL
fi

for t in $THEMES; do
  em++ -O2 -std=c++17 -Wall -I../core \
    ../core/*.cpp ../themes/"$t"/*.cpp wasm.cpp \
    -sMODULARIZE=1 -sEXPORT_ES6=1 -sEXPORT_NAME=createUi -sENVIRONMENT=web,node \
    -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=33554432 \
    -sEXPORTED_FUNCTIONS=_malloc,_free \
    -sEXPORTED_RUNTIME_METHODS=ccall,HEAPU8,HEAP16 \
    -o "build/$t.js"
  echo "built $t into $(pwd)/build/$t.js"
done

python3 -I ../tools/themes.py ../themes build/themes.json
