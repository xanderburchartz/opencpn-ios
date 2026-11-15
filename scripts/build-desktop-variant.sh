#!/usr/bin/env bash
# Bouwt één OpenCPN-revisie als macOS-desktopapp, los van alle andere bouwmappen.
#
# Gebruik: scripts/build-desktop-variant.sh <naam> <rev> [ninja -j]
#   <naam>  korte naam, bijv. kaal of fix-units
#   <rev>   commit of branch uit upstream/OpenCPN-fixes (bijv. 8c8d64570, fix/...)
#
# Bron:     upstream/desk-<naam> (git worktree van upstream/OpenCPN-fixes, detached)
# Bouwmap:  build/desktop/var-<naam>; app: build/desktop/var-<naam>/OpenCPN.app
# De app krijgt bundel-ID org.opencpn.iosporttest.<naam> en opencpn-glutil in
# Contents/libexec (anders zet OpenCPN OpenGL uit). Start altijd met een eigen
# configuratiemap: open -n <app> --args -c <map>.
# Vereist: Homebrew wxwidgets@3.2, ninja, cmake; ccache optioneel (cache in build/ccache).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NAAM="${1:?naam}"; REV="${2:?rev}"; JOBS="${3:-}"
[[ "$NAAM" =~ ^[a-z0-9-]+$ ]] || { echo "naam: alleen a-z, 0-9 en -" >&2; exit 1; }
SRC="$ROOT/upstream/desk-$NAAM"
BLD="$ROOT/build/desktop/var-$NAAM"
WX="$(brew --prefix wxwidgets@3.2)/bin/wx-config-3.2"

if [ -d "$SRC" ]; then
  git -C "$SRC" checkout -q --detach "$REV"
else
  git -C "$ROOT/upstream/OpenCPN-fixes" worktree add -q --detach "$SRC" "$REV"
fi
echo "bron $(git -C "$SRC" log -1 --format='%h %s')"

LAUNCHER_ARG=()
if command -v ccache >/dev/null 2>&1 && [ "${OCPNIOS_NO_CCACHE:-}" != 1 ]; then
  export CCACHE_DIR="${CCACHE_DIR:-$ROOT/build/ccache}"
  export CCACHE_BASEDIR="${CCACHE_BASEDIR:-$ROOT}"
  LAUNCHER_ARG=(-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache)
fi

if [ ! -f "$BLD/build.ninja" ]; then
  cmake -S "$SRC" -B "$BLD" -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug -DOCPN_CI_BUILD=OFF -DOCPN_RELEASE=0 \
    -DOCPN_BUILD_TEST=OFF -DOCPN_BUILD_SAMPLE=OFF -DOCPN_USE_SYSTEM_LIBARCHIVE=OFF \
    -DwxWidgets_CONFIG_EXECUTABLE="$WX" -DCMAKE_OSX_ARCHITECTURES=arm64 \
    ${LAUNCHER_ARG[@]+"${LAUNCHER_ARG[@]}"}
fi
s=$(date +%s)
ninja -C "$BLD" ${JOBS:+-j "$JOBS"}
echo "ninja klaar in $(( $(date +%s) - s )) s"

APP="$BLD/OpenCPN.app"
mkdir -p "$APP/Contents/libexec"
cp "$BLD/glutil/opencpn-glutil" "$APP/Contents/libexec/"
# Vertalingen: zonder install staan ze alleen in de bouwmap.
for mo in "$BLD"/opencpn_*.mo; do
  [ -e "$mo" ] || continue
  lang="$(basename "$mo" .mo)"; lang="${lang#opencpn_}"
  mkdir -p "$APP/Contents/Resources/$lang.lproj"
  cp "$mo" "$APP/Contents/Resources/$lang.lproj/opencpn.mo"
done
/usr/libexec/PlistBuddy -c "Set :CFBundleIdentifier org.opencpn.iosporttest.$NAAM" \
  "$APP/Contents/Info.plist"
codesign --force --deep -s - "$APP" 2>/dev/null
echo "app $APP"
