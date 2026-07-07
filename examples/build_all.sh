#!/usr/bin/env bash
# Build every atomics example. Pass 'tsan' as the first arg to also build the
# race/synchronization examples under ThreadSanitizer.
#   ./build_all.sh          # normal -O2 builds
#   ./build_all.sh tsan     # also run key examples under ThreadSanitizer
set -u
CXX="${CXX:-clang++}"
DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR"

STD="c++20"
COMMON="-Wall -Wextra -pthread"
PASS=0; FAIL=0

for f in ch*.cpp; do
  out="/tmp/$(basename "$f" .cpp)"
  if "$CXX" -std="$STD" -O2 $COMMON "$f" -o "$out" 2>/tmp/err.txt; then
    echo "OK    $f"; PASS=$((PASS+1))
  else
    echo "FAIL  $f"; sed 's/^/        /' /tmp/err.txt; FAIL=$((FAIL+1))
  fi
done

echo "-----------------------------------------"
echo "passed=$PASS failed=$FAIL"

if [ "${1:-}" = "tsan" ]; then
  echo
  echo "=== ThreadSanitizer builds (race detection) ==="
  for f in ch01_race.cpp ch04_message_passing.cpp ch09_fence.cpp ch11_spsc_queue.cpp; do
    out="/tmp/tsan_$(basename "$f" .cpp)"
    if "$CXX" -std="$STD" -O1 -g -fsanitize=thread $COMMON "$f" -o "$out" 2>/tmp/err.txt; then
      echo "TSAN OK  $f  ->  run: $out"
    else
      echo "TSAN FAIL $f"; sed 's/^/        /' /tmp/err.txt
    fi
  done
  echo "(ch01_race under TSan SHOULD report a data race in racy(); the others"
  echo " should be clean because they are correctly synchronized.)"
fi
