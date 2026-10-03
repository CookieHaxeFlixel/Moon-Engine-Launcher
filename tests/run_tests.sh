#!/usr/bin/env bash
# Portable logic tests (versions, zip extraction, install/preserve rules, showConsole parsing).
set -e
cd "$(dirname "$0")/.."
mkdir -p build-tests
for f in miniz miniz_tdef miniz_tinfl miniz_zip; do
    gcc -c -O1 third_party/miniz/$f.c -o build-tests/$f.o
done
g++ -std=c++17 -Wall -Wextra -I. -Ithird_party/miniz tests/update_logic_test.cpp build-tests/*.o -o build-tests/update_logic_test
./build-tests/update_logic_test
