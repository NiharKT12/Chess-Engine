#!/usr/bin/env bash
# Builds and runs the engine regression tests (no raylib required).
set -e
cd "$(dirname "$0")/.."
SRC="Chess Engine"
OUT="${TMPDIR:-/tmp}/chess_tests"
g++ -O2 -std=c++17 -Wall -I"$SRC" \
    tests/tests.cpp "$SRC/Board.cpp" "$SRC/MoveGen.cpp" "$SRC/Search.cpp" "$SRC/Zobrist.cpp" \
    -o "$OUT"
exec "$OUT"
