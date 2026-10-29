#!/bin/sh
# Builds a display program into build/ui.js and build/ui.wasm for the review
# page. APP picks it from devices/apps (halo by default).
# Needs Emscripten (brew install emscripten).
set -e
cd "$(dirname "$0")"
APP="${APP:-halo}"
if [ ! -d "../apps/$APP" ]; then
  echo "No display program ../apps/$APP" >&2
  exit 1
fi
mkdir -p build
em++ -O2 -std=c++17 -I../core \
  ../core/*.cpp ../apps/"$APP"/*.cpp wasm.cpp \
  -sMODULARIZE=1 -sEXPORT_ES6=1 -sEXPORT_NAME=createUi -sENVIRONMENT=web \
  -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=33554432 \
  -sEXPORTED_FUNCTIONS=_malloc,_free \
  -sEXPORTED_RUNTIME_METHODS=ccall,HEAPU8,HEAP16 \
  -o build/ui.js
echo "built $APP into $(pwd)/build/ui.js"
