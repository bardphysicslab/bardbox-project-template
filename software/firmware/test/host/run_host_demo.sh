#!/bin/sh
# Builds and runs the host demo: the unmodified student sketch (src/main.cpp)
# with the SIMULATED BME280, simulated time and a scripted serial host.
# Usage (from software/firmware): test/host/run_host_demo.sh
set -eu
here=$(cd "$(dirname "$0")" && pwd)
fw=$(cd "$here/../.." && pwd)
out=${TMPDIR:-/tmp}/bardbox-host-tests
mkdir -p "$out"
CXX=${CXX:-c++}
inc="-I$here/stub -I$fw/src"
srcs=""
for lib in BardBoxSerial BardBoxBuffer BardBoxNetwork BardBoxMeasurements BardBoxInterface; do
  inc="$inc -I$fw/lib/$lib/src"
  for f in "$fw/lib/$lib/src"/*.cpp; do [ -e "$f" ] && srcs="$srcs $f"; done
done
$CXX -std=c++11 -Wall -Wextra -Werror -DBARDBOX_HOST_DEMO \
  '-DFIRMWARE_VERSION="0.0.0-m1-host-demo"' -I"$here" $inc $srcs "$fw/src/main.cpp" "$here/demo_m1.cpp" \
  -o "$out/demo_m1"
"$out/demo_m1"

echo
echo "== durable recording across a simulated restart =="
$CXX -std=c++11 -Wall -Wextra -Werror -DBARDBOX_BUFFER_CAPACITY_RECORDS=4 -I"$here" $inc $srcs \
  "$here/demo_restart.cpp" -o "$out/demo_restart"
"$out/demo_restart"

echo
echo "== constant 42 through the actual sketch (SIMULATED delivery) =="
$CXX -std=c++11 -Wall -Wextra -Werror -DBARDBOX_HOST_DEMO \
  '-DFIRMWARE_VERSION="0.0.0-wifi42-host-demo"' -DBARDBOX_RECORDING_INTERVAL_S=5 \
  '-DBARDBOX_NETWORK_ENDPOINT="https://receiver.invalid/demo/wifi42"' \
  -I"$here" $inc $srcs "$fw/examples/wifi42/main.cpp" "$here/demo_wifi42.cpp" -o "$out/demo_wifi42"
"$out/demo_wifi42"
