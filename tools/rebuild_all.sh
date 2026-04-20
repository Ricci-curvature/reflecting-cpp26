#!/bin/bash
# Local verification script: rebuild + run every stage/test under
# clang-p2996 and confirm they all compile clean and pass.
# Usage (from WSL Ubuntu):  bash tools/rebuild_all.sh
set -e
cd "$(dirname "$0")/.."
CLANG=/home/user/src/clang-p2996/build/bin/clang++
FLAGS=(-std=c++26 -freflection-latest -stdlib=libc++ -nostdinc++
  -isystem /home/user/src/clang-p2996/build/include/x86_64-unknown-linux-gnu/c++/v1
  -isystem /home/user/src/clang-p2996/build/include/c++/v1
  -L /home/user/src/clang-p2996/build/lib/x86_64-unknown-linux-gnu
  -Wl,-rpath,/home/user/src/clang-p2996/build/lib/x86_64-unknown-linux-gnu
  -I include)

for src in \
    stages/19_constexpr_validation.cpp \
    stages/20_json_schema_export.cpp \
    tests/smoke_test.cpp \
    tests/protocol_smoke_test.cpp \
    tests/constexpr_smoke_test.cpp \
    tests/json_schema_smoke_test.cpp \
    tests/readme_example.cpp
do
    echo "=== $src ==="
    "$CLANG" "${FLAGS[@]}" -o /tmp/bin "$src"
    /tmp/bin
done
