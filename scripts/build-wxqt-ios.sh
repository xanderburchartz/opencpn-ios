#!/usr/bin/env bash
# Bouwt wxWidgets (upstream/wxWidgets) met de Qt-toolkit als statische bibliotheken voor iOS.
#
# Gebruik:
#   scripts/build-wxqt-ios.sh [sim|device|device-release] [configure|build|all]
#
# device-release: Release-bouw voor verspreiding (TestFlight), eigen map build/device-release.
#
# Omgeving:
#   QT_ROOT   map met de Qt-versie (standaard ~/Qt/6.12.0)
#
# Uitvoer: build/<sim|device>/wxqt (bouwmap), build/<sim|device>/wxqt-install (installatie),
# logs in build/logs/. Patches uit patches/wxWidgets moeten al toegepast zijn
# (scripts/apply-patches.sh).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TARGET="${1:-sim}"
STEP="${2:-all}"
QT_ROOT="${QT_ROOT:-$HOME/Qt/6.12.0}"

BUILD_TYPE=Debug
case "$TARGET" in
  sim) QT_KIT="$QT_ROOT/ios_simulator_arm64" ;;
  device) QT_KIT="$QT_ROOT/ios_device" ;;
  device-release) QT_KIT="$QT_ROOT/ios_device"; BUILD_TYPE=Release ;;
  *) echo "onbekend doel: $TARGET (sim, device of device-release)" >&2; exit 1 ;;
esac
[ -x "$QT_KIT/bin/qt-cmake" ] || { echo "geen qt-cmake in $QT_KIT" >&2; exit 1; }

SRC="$ROOT/upstream/wxWidgets"
BLD="$ROOT/build/$TARGET/wxqt"
INST="$ROOT/build/$TARGET/wxqt-install"
LOGS="$ROOT/build/logs"
mkdir -p "$BLD" "$LOGS"

configure() {
  "$QT_KIT/bin/qt-cmake" -S "$SRC" -B "$BLD" -G Ninja \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=18.0 \
    -DCMAKE_INSTALL_PREFIX="$INST" \
    -DwxBUILD_TOOLKIT=qt \
    -DwxUSE_STD_CONTAINERS=OFF \
    -DwxBUILD_SHARED=OFF \
    -DwxBUILD_SAMPLES=OFF \
    -DwxBUILD_TESTS=OFF \
    -DwxBUILD_DEMOS=OFF \
    -DwxBUILD_BENCHMARKS=OFF \
    -DwxUSE_WEBVIEW=OFF \
    -DwxUSE_MEDIACTRL=OFF \
    -DwxUSE_STC=OFF \
    -DwxUSE_RIBBON=OFF \
    -DwxUSE_PROPGRID=OFF \
    -DwxUSE_FSWATCHER=OFF \
    -DwxUSE_JOYSTICK=OFF \
    -DwxUSE_DIALUP_MANAGER=OFF \
    -DwxUSE_SECRETSTORE=OFF \
    -DwxUSE_LIBSDL=OFF \
    -DwxUSE_LIBNOTIFY=OFF \
    -DwxUSE_LIBMSPACK=OFF \
    -DwxUSE_LIBLZMA=OFF \
    -DwxUSE_ZLIB=builtin \
    -DwxUSE_LIBPNG=builtin \
    -DwxUSE_LIBJPEG=sys \
    -DJPEG_INCLUDE_DIR="$QT_KIT/include/QtJpeg" \
    -DJPEG_LIBRARY="$QT_KIT/lib/libQt6BundledLibjpeg.a" \
    -DwxUSE_LIBTIFF=builtin \
    -DwxUSE_LIBWEBP=builtin \
    -DwxUSE_EXPAT=builtin \
    -DwxUSE_REGEX=builtin \
    -DwxUSE_NANOSVG=builtin \
    -DwxUSE_OPENGL=ON
}

build() {
  cmake --build "$BLD"
  cmake --install "$BLD"
}

case "$STEP" in
  configure) configure ;;
  build) build ;;
  all) configure; build ;;
  *) echo "onbekende stap: $STEP" >&2; exit 1 ;;
esac
