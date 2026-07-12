#!/usr/bin/env bash
# Build the 2048-ranks solver as WebAssembly for the web version.
#
# Requires Emscripten (https://emscripten.org). If em++ is not on PATH the
# script tries the standard emsdk location (~/emsdk).
#
# Output: web/solver/2048-ranks.mjs + web/solver/2048-ranks.wasm

set -euo pipefail
cd "$(dirname "$0")/.."

if ! command -v em++ >/dev/null 2>&1; then
    if [ -f "$HOME/emsdk/emsdk_env.sh" ]; then
        # shellcheck disable=SC1091
        source "$HOME/emsdk/emsdk_env.sh" >/dev/null 2>&1
    else
        echo "em++ not found. Install emsdk: https://emscripten.org/docs/getting_started/downloads.html" >&2
        exit 1
    fi
fi

mkdir -p web/solver

# -fwasm-exceptions: the solver relies on C++ exceptions (JSON parsing,
# std::stoi...); Emscripten turns exceptions into abort() unless enabled.
em++ -std=c++17 -O3 -fwasm-exceptions \
    src/board.cpp src/evaluator.cpp src/main.cpp src/move.cpp src/solver.cpp src/stat.cpp \
    -o web/solver/2048-ranks.mjs \
    -sMODULARIZE=1 \
    -sEXPORT_ES6=1 \
    -sEXPORT_NAME=createSolverModule \
    -sINVOKE_RUN=0 \
    -sEXPORTED_RUNTIME_METHODS=callMain,FS \
    -sALLOW_MEMORY_GROWTH=1 \
    -sENVIRONMENT=web,worker,node \
    -sSTACK_SIZE=1048576

ls -lh web/solver/
