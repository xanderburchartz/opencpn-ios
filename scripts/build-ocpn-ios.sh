#!/usr/bin/env bash
# Bouwt OpenCPN voor iOS met app/CMakeLists.txt.
#
# Gebruik: scripts/build-ocpn-ios.sh [sim|device|device-release] [configure|build|all] [-- extra ninja-args]
#
# device-release: Release-bouw voor verspreiding (TestFlight) in build/device-release; gebruikt
# de afhankelijkheden van build/device/deps-install (die zijn al Release).
#
# Maakt build/<doel>/ocpn-src: symlinks naar alles in upstream/OpenCPN, behalve
# CMakeLists.txt (dat wijst naar app/CMakeLists.txt). Bouwmap: build/<doel>/ocpn.
# Vereist: scripts/build-wxqt-ios.sh <doel> en scripts/build-deps-ios.sh <doel>.
# Omgeving: QT_ROOT (standaard ~/Qt/6.12.0).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TARGET="${1:-sim}"
STEP="${2:-all}"
shift $(( $# >= 2 ? 2 : $# ))
[ "${1:-}" = "--" ] && shift
QT_ROOT="${QT_ROOT:-$HOME/Qt/6.12.0}"
BUILD_TYPE=Debug
DEPS_TARGET="$TARGET"
case "$TARGET" in
  sim) QT_KIT="$QT_ROOT/ios_simulator_arm64" ;;
  device) QT_KIT="$QT_ROOT/ios_device" ;;
  device-release) QT_KIT="$QT_ROOT/ios_device"; BUILD_TYPE=Release; DEPS_TARGET=device ;;
  *) echo "onbekend doel: $TARGET" >&2; exit 1 ;;
esac
# Eigen bundel-ID-voorvoegsel (iedere ontwikkelaar heeft een eigen ID nodig), bijv.
# OCPNIOS_BUNDLE_PREFIX=nl.voorbeeld.opencpnios. Standaard: local.opencpnios.
BUNDLE_ARG=()
[ -n "${OCPNIOS_BUNDLE_PREFIX:-}" ] && BUNDLE_ARG=(-DOCPNIOS_BUNDLE_PREFIX="$OCPNIOS_BUNDLE_PREFIX")

# OCPNIOS_OCPN_SRC: andere OpenCPN-bron (bijv. upstream/OpenCPN-master voor de hoofdreeks);
# OCPNIOS_BUILD_SUFFIX: achtervoegsel voor bron- en bouwmap (bijv. "-master"), zodat die
# build de gewone niet raakt.
UPSRC="${OCPNIOS_OCPN_SRC:-$ROOT/upstream/OpenCPN}"
FARM="$ROOT/build/$TARGET/ocpn-src${OCPNIOS_BUILD_SUFFIX:-}"
BLD="$ROOT/build/$TARGET/ocpn${OCPNIOS_BUILD_SUFFIX:-}"
[ -d "$UPSRC/gui" ] || { echo "$UPSRC ontbreekt" >&2; exit 1; }

# ccache (optioneel): cache in build/ccache, niet in de repo. Max. grootte staat in
# build/ccache/ccache.conf (ccache -M 20G). Geldt pas na een nieuwe configure (stap all).
LAUNCHER_ARG=()
if command -v ccache >/dev/null 2>&1 && [ "${OCPNIOS_NO_CCACHE:-}" != 1 ]; then
  export CCACHE_DIR="${CCACHE_DIR:-$ROOT/build/ccache}"
  export CCACHE_BASEDIR="${CCACHE_BASEDIR:-$ROOT}"
  LAUNCHER_ARG=(-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache)
fi

make_farm() {
  mkdir -p "$FARM"
  local e name
  for e in "$UPSRC"/* "$UPSRC"/.[!.]*; do
    [ -e "$e" ] || continue
    name="$(basename "$e")"
    [ "$name" = "CMakeLists.txt" ] && continue
    [ "$name" = ".git" ] && continue
    ln -sfn "$e" "$FARM/$name"
  done
  ln -sfn "$ROOT/app/CMakeLists.txt" "$FARM/CMakeLists.txt"
}

copy_fetched() {
  # OpenCPN haalt shapelib en rapidjson anders zelf op (FetchContent) en patcht ze daarna.
  # We geven verse kopieën uit upstream/ mee, zodat de patches upstream/ niet raken.
  local SRCS="$ROOT/build/$TARGET/fetched-src" n
  mkdir -p "$SRCS"
  for n in shapelib rapidjson; do
    [ -d "$ROOT/upstream/$n" ] || { echo "upstream/$n ontbreekt (fetch-upstream.sh $n)" >&2; exit 1; }
    rm -rf "${SRCS:?}/$n"
    rsync -a --exclude .git "$ROOT/upstream/$n/" "$SRCS/$n/"
  done
}

configure() {
  make_farm
  copy_fetched
  # gpatch-vervanger (git apply) voor OpenCPN's PatchFile.cmake, zie scripts/tools/gpatch
  export PATH="$ROOT/scripts/tools:$PATH"
  "$QT_KIT/bin/qt-cmake" -S "$FARM" -B "$BLD" -G Ninja \
    -DFETCHCONTENT_FULLY_DISCONNECTED=ON \
    -DFETCHCONTENT_SOURCE_DIR_SHAPELIB_SRC="$ROOT/build/$TARGET/fetched-src/shapelib" \
    -DFETCHCONTENT_SOURCE_DIR_RAPIDJSON_SRC="$ROOT/build/$TARGET/fetched-src/rapidjson" \
    ${OCPNIOS_JSON_SRC:+-DFETCHCONTENT_SOURCE_DIR_JSON="$OCPNIOS_JSON_SRC"} \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=18.0 \
    -DOCPNIOS_ROOT="$ROOT" \
    -DOCPNIOS_DEPS="$ROOT/build/$DEPS_TARGET/deps-install" \
    -DwxWidgets_DIR="$ROOT/build/$TARGET/wxqt-install/lib/cmake/wxWidgets-3.3" \
    ${BUNDLE_ARG[@]+"${BUNDLE_ARG[@]}"} \
    ${LAUNCHER_ARG[@]+"${LAUNCHER_ARG[@]}"}
}

build() {
  cmake --build "$BLD" -- -k 0 "$@"
}

case "$STEP" in
  configure) configure ;;
  build) build "$@" ;;
  all) configure; build "$@" ;;
  *) echo "onbekende stap: $STEP" >&2; exit 1 ;;
esac
