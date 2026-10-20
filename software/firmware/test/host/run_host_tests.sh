#!/bin/sh
# Builds and runs the BardBox host tests with the system C++ compiler.
# Usage (from software/firmware): test/host/run_host_tests.sh
set -eu
here=$(cd "$(dirname "$0")" && pwd)
fw=$(cd "$here/../.." && pwd)
out=${TMPDIR:-/tmp}/bardbox-host-tests
mkdir -p "$out"
CXX=${CXX:-c++}
inc="-I$here/stub"
srcs=""
for lib in BardBoxSerial BardBoxBuffer BardBoxNetwork BardBoxMeasurements BardBoxInterface; do
  inc="$inc -I$fw/lib/$lib/src"
  for f in "$fw/lib/$lib/src"/*.cpp; do [ -e "$f" ] && srcs="$srcs $f"; done
done
# Host tests never read the developer's private include/config.h: module
# defaults apply because include/ is not on this include path.
$CXX -std=c++17 -Wall -Wextra -Werror $inc $srcs "$here/test_m1.cpp" -o "$out/test_m1"
"$out/test_m1"

# The version guard must stop a build with no or an empty FIRMWARE_VERSION.
for body in '' '#define FIRMWARE_VERSION ""'; do
  printf '%s\n#include <BardBoxInterface.h>\nint main(){BardBoxInterface b; return b.begin();}\n' "$body" > "$out/version_guard.cpp"
  if $CXX -std=c++17 $inc -fsyntax-only "$out/version_guard.cpp" 2> "$out/version_guard.log"; then
    echo "version guard did not fail for: [$body]" >&2; exit 1
  fi
  grep -q "Please set FIRMWARE_VERSION in main.cpp." "$out/version_guard.log" || {
    echo "version guard message missing for: [$body]" >&2; cat "$out/version_guard.log" >&2; exit 1; }
done
echo "version guard: missing and empty FIRMWARE_VERSION both fail with the required message"
