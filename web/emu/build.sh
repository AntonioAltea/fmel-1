#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# MEL in the browser: firmware/src/mel.c against a fake FM-1 (web/emu/fm1_web.c), compiled to WebAssembly.
#   web/emu/build.sh            ->  build/emu/{index.html, worklet.js, mel.wasm}
#   python3 -m http.server -d build/emu 8790     (then http://localhost:8790/)
# The compiler is Zig's clang (pip install ziglang; ZIG names another command); the tapes are tools/gen_tapes.py's.
set -e
cd "$(dirname "$0")/../.."
ZIG="${ZIG:-.venv-zig/bin/python -m ziglang}"
mkdir -p build/emu build/gen
[ -f build/gen/mel_tapes.h ] || python3 tools/gen_tapes.py build/gen/mel_tapes.h
# shellcheck disable=SC2086
$ZIG cc -target wasm32-freestanding -O2 -nostdlib -fno-builtin -Wall -Wno-unused-function \
    -DDINGHY_WEB "-DAPP_VER=\"${APP_VER:-web}\"" -Ifirmware/src -Ifirmware/gen -Ibuild/gen \
    -Wl,--no-entry -Wl,--export-dynamic -Wl,-z,stack-size=1048576 -Wl,--initial-memory=33554432 \
    -o build/emu/mel.wasm firmware/src/mel.c
cp web/emu/index.html web/emu/worklet.js build/emu/
echo "emu: build/emu ($(wc -c < build/emu/mel.wasm | tr -d ' ') B wasm)"
