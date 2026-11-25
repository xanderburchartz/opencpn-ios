#!/bin/bash
# Android stap A: bouwt de OpenCPN-kernbibliotheek libgorp.so voor Android (arm64 of armhf)
# op de Mac met de NDK, zoals de CircleCI-job van OpenCPN dat doet
# (ci/circleci-build-android-corelib-armhf.sh). Geen APK; dat is stap B.
#
# Gebruik:
#   scripts/build-android-core.sh <naam> <bron-repo> <ref> [arm64|armhf]
#   scripts/build-android-core.sh master     # = upstream/OpenCPN-master ios-port-master arm64
#   scripts/build-android-core.sh upstream   # = upstream/OpenCPN-master origin/master arm64 (nulmeting)
# Omgeving: NDK (standaard ~/navico_autopilot_pi/cache/ndk/android-ndk-r27d), JOBS.
#
# Twee bouwhulpen, alleen in de bouwkopie, niet in de fork:
# - libs/AndroidLibs.cmake downloadt OCPNAndroidCoreBuildSupport.zip (310 MB) bij élke
#   configure ("if (TRUE)"). Hier wordt dat "alleen als hij ontbreekt", met de zip die al in
#   build/android/cache staat.
# - buildandroid/build_android.cmake verwacht aarch64-linux-android-ar, die in NDK r23+ niet
#   meer bestaat. Een tool_base-map met symlinks wijst die naam naar llvm-ar.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
NDK="${NDK:-$HOME/navico_autopilot_pi/cache/ndk/android-ndk-r27d}"
JOBS="${JOBS:-12}"
case "${1:-}" in
  master)   set -- master "$ROOT/upstream/OpenCPN-master" ios-port-master arm64 ;;
  upstream) set -- upstream "$ROOT/upstream/OpenCPN-master" origin/master arm64 ;;
esac
[ $# -ge 3 ] || { sed -n 2,19p "$0"; exit 2; }
NAAM="$1"; REPO="$(cd "$2" && pwd)"; REF="$3"; ARCH="${4:-arm64}"
case "$NAAM" in ''|*/*|.*) echo "ongeldige naam" >&2; exit 2 ;; esac
case "$ARCH" in arm64|armhf) ;; *) echo "arch: arm64 of armhf" >&2; exit 2 ;; esac

OUT="$ROOT/build/android"
CACHE="$OUT/cache"
ZIP="$CACHE/OCPNAndroidCoreBuildSupport.zip"
[ -f "$ZIP" ] || { echo "ontbreekt: $ZIP (zie docs/platformbuilds.md)" >&2; exit 1; }
[ -e "$CACHE/support.zip" ] || ln -s OCPNAndroidCoreBuildSupport.zip "$CACHE/support.zip"
[ -d "$CACHE/OCPNAndroidCoreBuildSupport" ] || (cd "$CACHE" && cmake -E tar -xzf support.zip)

# tool_base met de oude namen.
TB_NDK="$NDK/toolchains/llvm/prebuilt/darwin-x86_64"
TB="$OUT/tool_base"
if [ ! -d "$TB/bin" ]; then
  mkdir -p "$TB/bin"
  for f in "$TB_NDK"/bin/*; do ln -s "$f" "$TB/bin/"; done
  ln -s "$TB_NDK/bin/llvm-ar" "$TB/bin/aarch64-linux-android-ar"
  ln -s "$TB_NDK/sysroot" "$TB/sysroot"
  ln -s "$TB_NDK/lib" "$TB/lib"
fi

SRC="$OUT/src-$NAAM"
if [ -d "$SRC/.git" ]; then
  git -C "$SRC" checkout -q -- . ; git -C "$SRC" fetch -q origin
else
  git clone -q --shared --no-checkout "$REPO" "$SRC"
fi
git -C "$SRC" checkout -q --detach "$(git -C "$REPO" rev-parse "$REF^{commit}")"
REV="$(git -C "$SRC" rev-parse --short HEAD)"
sed -i '' 's|^if (TRUE) #(NOT EXISTS ${OCPN_ANDROID_CACHEDIR}/support.zip)|if (NOT EXISTS ${OCPN_ANDROID_CACHEDIR}/support.zip)|; s|^if (TRUE) #(NOT EXISTS ${_master_base})|if (NOT EXISTS ${_master_base})|' \
  "$SRC/libs/AndroidLibs.cmake"
# CMakeLists.txt kiest de doelnaam (gorp) met "if (APPLE)" vóór project(). CMake 4 zet APPLE
# op een Mac als bouwhost daar al (gemeten; na project() met de Generic-toolchain is hij
# leeg), waardoor het doel OpenCPN heet en de plugins libgorp.so niet vinden. De CI bouwt
# op Linux. Alleen die ene regel, alleen in de bouwkopie.
sed -i '' '1,80s|^if (APPLE)$|if (APPLE AND NOT "${OCPN_TARGET_TUPLE}" MATCHES "Android")|' "$SRC/CMakeLists.txt"
grep -q 'if (APPLE AND NOT "${OCPN_TARGET_TUPLE}"' "$SRC/CMakeLists.txt" || { echo "APPLE-regel niet gevonden" >&2; exit 1; }
echo "== android $ARCH $NAAM: $REPO $REF ($REV)"

TUPLE="Android-$ARCH;16;$ARCH"
BLD="$OUT/$NAAM-$ARCH"
[ "${KEEP_BUILD:-0}" = 1 ] || rm -rf "$BLD"; mkdir -p "$BLD"; cd "$BLD"
cmake "$SRC" -DCMAKE_BUILD_TYPE=Release \
  -DOCPN_TARGET_TUPLE:STRING="$TUPLE" \
  -Dtool_base="$TB" -DOCPN_ANDROID_CACHEDIR="$CACHE" \
  -DCMAKE_CXX_FLAGS="-Wno-error=format-truncation" \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  -DCMAKE_TOOLCHAIN_FILE="$SRC/buildandroid/build_android.cmake"
# - CMAKE_POLICY_VERSION_MINIMUM: AndroidLibs.cmake vraagt 3.1; CMake 4 weigert < 3.5.
# - format-truncation: android/androidUTIL.cpp:4443 (char sb[400]) is te klein voor de
#   slider-stylesheet (>= 426 tekens). Upstream-fout; de clang van NDK r27 meldt hem,
#   -Werror maakt er een bouwfout van. Hier alleen weer een waarschuwing.
make lunasvg
# De ingebouwde plugins linken tegen ../../libgorp.so zonder afhankelijkheid op het doel;
# met -j moet gorp dus eerst.
make -j"$JOBS" gorp
make -j"$JOBS"
ls -l "$BLD"/libgorp.so
echo "$REPO $REF $REV $ARCH $(date -u +%FT%TZ)" >> "$OUT/$NAAM-$ARCH.bouwlog"
echo "== klaar: $BLD/libgorp.so ($REV)"
