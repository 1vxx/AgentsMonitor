#!/bin/sh
# Builds the UI core into build/ui.js and build/ui.wasm for the review page.
# Needs Emscripten (brew install emscripten).
set -e
cd "$(dirname "$0")"
mkdir -p build
em++ -O2 -std=c++17 \
  ../ui/gfx.cpp ../ui/text.cpp ../ui/model.cpp ../ui/synth.cpp ../ui/halo.cpp ../ui/ui.cpp wasm.cpp \
  -sMODULARIZE=1 -sEXPORT_ES6=1 -sEXPORT_NAME=createUi -sENVIRONMENT=web \
  -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=33554432 \
  -sEXPORTED_FUNCTIONS=_malloc,_free \
  -sEXPORTED_RUNTIME_METHODS=ccall,HEAPU8,HEAP16 \
  -o build/ui.js
echo "built $(pwd)/build/ui.js"
