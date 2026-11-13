#!/usr/bin/env bash
# Bouwt een spike uit spikes/<naam> als iOS-app met Xcode.
#
# Gebruik: scripts/build-spike.sh <naam> [sim|device]
#   sim:    ongetekend, voor de simulator (arm64)
#   device: getekend met het team uit OCPNIOS_TEAM_ID (verplicht), voor iPhone/iPad
#
# Omgeving: QT_ROOT (standaard ~/Qt/6.12.0), OCPNIOS_TEAM_ID (alleen device).
# Uitvoer: build/<sim|device>/<naam>/Debug-<sdk>/<doel>.app, logs in build/logs/.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NAME="${1:?naam van de spike}"
TARGET="${2:-sim}"
QT_ROOT="${QT_ROOT:-$HOME/Qt/6.12.0}"

case "$TARGET" in
  sim) QT_KIT="$QT_ROOT/ios_simulator_arm64"; SDK=iphonesimulator ;;
  device) QT_KIT="$QT_ROOT/ios_device"; SDK=iphoneos ;;
  *) echo "onbekend doel: $TARGET" >&2; exit 1 ;;
esac

SRC="$ROOT/spikes/$NAME"
BLD="$ROOT/build/$TARGET/$NAME"
WXDIR="$ROOT/build/$TARGET/wxqt-install/lib/cmake/wxWidgets-3.3"
[ -f "$SRC/CMakeLists.txt" ] || { echo "geen spike $SRC" >&2; exit 1; }
mkdir -p "$BLD" "$ROOT/build/logs"

"$QT_KIT/bin/qt-cmake" -S "$SRC" -B "$BLD" -G Xcode \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=18.0 \
  -DCMAKE_CONFIGURATION_TYPES=Debug \
  -DwxWidgets_DIR="$WXDIR"

if [ "$TARGET" = sim ]; then
  cmake --build "$BLD" --config Debug -- -sdk "$SDK" -arch arm64 CODE_SIGNING_ALLOWED=NO
else
  : "${OCPNIOS_TEAM_ID:?zet OCPNIOS_TEAM_ID op het ontwikkelteam}"
  cmake --build "$BLD" --config Debug -- -sdk "$SDK" -arch arm64 \
    DEVELOPMENT_TEAM="$OCPNIOS_TEAM_ID" CODE_SIGN_STYLE=Automatic -allowProvisioningUpdates
fi
