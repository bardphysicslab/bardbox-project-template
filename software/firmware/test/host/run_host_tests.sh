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
for lib in BardBoxSerial BardBoxBuffer BardBoxNetwork BardBoxMeasurements BardBoxSettings BardBoxInterface; do
  inc="$inc -I$fw/lib/$lib/src"
  for f in "$fw/lib/$lib/src"/*.cpp; do [ -e "$f" ] && srcs="$srcs $f"; done
done
# Host tests never read the developer's private include/config.h: module
# defaults apply because include/ is not on this include path.
$CXX -std=c++11 -Wall -Wextra -Werror -I"$here" $inc $srcs "$here/test_m1.cpp" -o "$out/test_m1"
"$out/test_m1"

# Saved device settings (UID, Wi-Fi) and the CONFIG_* serial commands.
$CXX -std=c++11 -Wall -Wextra -Werror -I"$here" $inc $srcs "$here/test_settings.cpp" -o "$out/test_settings"
"$out/test_settings"

# Durable Buffer tests with a small capacity so saturation is exercised.
$CXX -std=c++11 -Wall -Wextra -Werror -DBARDBOX_BUFFER_CAPACITY_RECORDS=4 \
  -I"$here" -I"$fw/lib/BardBoxBuffer/src" "$fw/lib/BardBoxBuffer/src/BardBoxBuffer.cpp" \
  "$fw/lib/BardBoxBuffer/src/BardBoxRecordCodec.cpp" "$here/test_buffer.cpp" -o "$out/test_buffer"
"$out/test_buffer"

# The FW-M2 review reproductions, adapted to the corrected API.
$CXX -std=c++11 -Wall -Wextra -Werror -DBARDBOX_BUFFER_CAPACITY_RECORDS=4 \
  -I"$here" -I"$fw/lib/BardBoxBuffer/src" "$fw/lib/BardBoxBuffer/src/BardBoxBuffer.cpp" \
  "$fw/lib/BardBoxBuffer/src/BardBoxRecordCodec.cpp" "$here/review_m2_repro.cpp" -o "$out/review_m2_repro"
"$out/review_m2_repro"

# An explicit project Wi-Fi setting, even empty, wins over the BardWireless default.
printf '#include "BardBoxNetworkConfig.h"\nstatic_assert(sizeof(BARDBOX_WIFI_SSID) == 1, "project SSID not respected");\nint main(){}\n' > "$out/wifi_default.cpp"
$CXX -std=c++11 -fsyntax-only -DBARDBOX_WIFI_SSID='""' $inc "$out/wifi_default.cpp"
echo "wifi default: explicit project BARDBOX_WIFI_SSID is respected"

# The version guard must stop a build with no or an empty FIRMWARE_VERSION.
for body in '' '#define FIRMWARE_VERSION ""'; do
  printf '%s\n#include <BardBoxInterface.h>\nint main(){BardBoxInterface b; return b.begin();}\n' "$body" > "$out/version_guard.cpp"
  if $CXX -std=c++11 $inc -fsyntax-only "$out/version_guard.cpp" 2> "$out/version_guard.log"; then
    echo "version guard did not fail for: [$body]" >&2; exit 1
  fi
  grep -q "Please set FIRMWARE_VERSION in main.cpp." "$out/version_guard.log" || {
    echo "version guard message missing for: [$body]" >&2; cat "$out/version_guard.log" >&2; exit 1; }
done
echo "version guard: missing and empty FIRMWARE_VERSION both fail with the required message"

# Network owner with a scripted fake transport. The endpoint is a placeholder
# that is never contacted; deadlines and backoff are shortened.
$CXX -std=c++11 -Wall -Wextra -Werror -DBARDBOX_BUFFER_CAPACITY_RECORDS=8 -DBARDBOX_RECORDING_INTERVAL_S=5 \
  '-DBARDBOX_NETWORK_ENDPOINT="https://receiver.invalid/upload"' \
  -DBARDBOX_NETWORK_ASSOCIATION_TIMEOUT_MS=1000 -DBARDBOX_NETWORK_DNS_TIMEOUT_MS=500 \
  -DBARDBOX_NETWORK_CONNECT_TIMEOUT_MS=700 -DBARDBOX_NETWORK_TLS_TIMEOUT_MS=600 \
  -DBARDBOX_NETWORK_REQUEST_TIMEOUT_MS=400 -DBARDBOX_NETWORK_RESPONSE_TIMEOUT_MS=800 \
  -DBARDBOX_NETWORK_RETRY_BASE_MS=200 -DBARDBOX_NETWORK_RETRY_MAX_MS=1600 -DBARDBOX_NETWORK_IDLE_POLL_MS=100 \
  -I"$here" $inc $srcs "$here/test_network.cpp" -o "$out/test_network"
"$out/test_network" "$out/delivery_sample.json"
if command -v python3 >/dev/null 2>&1; then
  python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); print("sample delivery body is valid JSON:", d["format"], "fw=" + d["firmware"], "queue=%s/%s" % (d["queue"]["generation"], d["queue"]["seq"]), "channels=%d" % len(d["channels"]))' "$out/delivery_sample.json"
fi

# Actual HTTPS adapter policy with controlled Wi-Fi/DNS/TLS I/O, not a socket.
$CXX -std=c++11 -Wall -Wextra -Werror -I"$fw/lib/BardBoxNetwork/src" \
  -I"$fw/lib/BardBoxBuffer/src" "$fw/lib/BardBoxNetwork/src/BardBoxWifiTransport.cpp" \
  "$here/test_wifi.cpp" -o "$out/test_wifi"
"$out/test_wifi"

# The actual device provider compiled against controlled platform API stubs.
$CXX -std=c++11 -Wall -Wextra -Werror -DARDUINO -DESP_PLATFORM \
  '-DBARDBOX_WIFI_SSID="synthetic-network"' \
  '-DBARDBOX_NETWORK_ENDPOINT="https://receiver.invalid/test"' \
  '-DBARDBOX_NETWORK_CA_PEM="synthetic trust anchor"' \
  -I"$here/wifi_stub" -I"$fw/lib/BardBoxNetwork/src" -I"$fw/lib/BardBoxBuffer/src" \
  "$fw/lib/BardBoxNetwork/src/BardBoxWifiTransport.cpp" \
  "$fw/lib/BardBoxNetwork/src/BardBoxEsp32Wifi.cpp" "$here/test_esp32_wifi.cpp" -o "$out/test_esp32_wifi"
"$out/test_esp32_wifi"
