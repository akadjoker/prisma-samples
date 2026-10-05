#!/bin/sh
root="$(cd "$(dirname "$0")" && pwd)"
build="${1:-build}"
frames="${2:-120}"
backend="$3"

cd "$root" || exit 1

if [ ! -f "$build/CMakeCache.txt" ]; then
    cmake -S . -B "$build" -DCMAKE_BUILD_TYPE=Release || exit 1
fi
cmake --build "$build" -j "${JOBS:-$(nproc)}" || exit 1

passed=0
failed=0
skipped=0
failures=""
for folder in [0-9][0-9]_*; do
    name="${folder}"
    name="${name#[0-9][0-9]_}"
    exe="$build/$name"
    if [ ! -x "$exe" ]; then
        echo "skip  $name (not built)"
        skipped=$((skipped + 1))
        continue
    fi
    echo "run   $name"
    "$exe" "$frames" $backend
    code=$?
    if [ $code -eq 0 ]; then
        passed=$((passed + 1))
    else
        echo "FAIL  $name (exit $code)"
        failed=$((failed + 1))
        failures="$failures $name"
    fi
done

echo
echo "demos: $passed passed, $failed failed, $skipped skipped"
[ $failed -eq 0 ] || { echo "failed:$failures"; exit 1; }
