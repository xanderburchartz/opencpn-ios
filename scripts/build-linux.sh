#!/bin/bash
# Bouwt OpenCPN of een plugin voor Linux arm64 (Debian 13 trixie) in een Docker-container
# op de Mac. Uitvoer: build/linux/<naam>/install<prefix> en een tarball
# build/linux/<naam>.tar.gz die scripts/navpi-test.sh naar de navpi zet.
#
# Prefix: OpenCPN gebruikt de PREFIX uit de build als installatieprefix (zie
# model/src/base_platform.cpp, SetInstallPrefix), de boom is dus niet verplaatsbaar. Daarom
# is de prefix de map waar hij op de navpi komt: $NAVPI_TESTDIR/<naam>, uit
# ~/.config/opencpn-ios/navpi.env (zie docs/platformbuilds.md). Zonder dat bestand of met
# PREFIX=... in de omgeving geldt die prefix (standaard /usr/local).
#
# Gebruik:
#   scripts/build-linux.sh <soort> <naam> <bron-repo> <ref>
#     soort     opencpn | plugin
#     naam      vrije naam voor de bouwmap, bijv. master, release, tactics, navico
#     bron-repo lokale git-repo (wordt met --shared gekloond, de bron blijft onaangeroerd)
#     ref       branch, tag of commit, of "werkboom" voor de map zoals hij is (met
#               ongecommitte wijzigingen; bouwmappen build*/ en cache/ gaan niet mee)
#   scripts/build-linux.sh master     # = opencpn master upstream/OpenCPN-master ios-port-master
#   scripts/build-linux.sh release    # = opencpn release upstream/OpenCPN-port ios-port-topics
#
# Plugins komen bij de OpenCPN-build die je met CORE=<naam> opgeeft (standaard master), in
# build/linux/<core>/userplugins (lib/opencpn, share/opencpn/plugins, share/locale).
# navpi-test.sh zet die in ~/.local van de test-HOME: OpenCPN laadt externe plugins niet uit
# zijn eigen <prefix>/lib/opencpn (model/src/plugin_loader.cpp, "Skipping plugin").
# Omgeving: JOBS (standaard 12), CORE, KEEP_BUILD=1 (bouwmap niet leegmaken),
#           STRIP=0 (debug-info houden).
# Voorbeelden plugins:
#   scripts/build-linux.sh plugin navico ~/navico_autopilot_pi main
#   scripts/build-linux.sh plugin tactics ~/tactics_pi werkboom
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMAGE="opencpn-ios/linux-bouw:trixie"
JOBS="${JOBS:-12}"
CONF="$HOME/.config/opencpn-ios/navpi.env"
# shellcheck disable=SC1090
[ -f "$CONF" ] && . "$CONF"

case "${1:-}" in
  master)  set -- opencpn master "$ROOT/upstream/OpenCPN-master" ios-port-master ;;
  release) set -- opencpn release "$ROOT/upstream/OpenCPN-port" ios-port-topics ;;
esac
[ $# -eq 4 ] || { sed -n 2,20p "$0"; exit 2; }
SOORT="$1"; NAAM="$2"; REPO="$(cd "$3" && pwd)"; REF="$4"
case "$NAAM" in ''|*/*|.*) echo "ongeldige naam: $NAAM" >&2; exit 2 ;; esac

OUT="$ROOT/build/linux"
mkdir -p "$OUT" "$ROOT/build/logs"

# Image bouwen als het er nog niet is.
if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
  cp "$ROOT/upstream/OpenCPN-master/ci/control" "$ROOT/scripts/linux/control"
  docker build --platform linux/arm64 -t "$IMAGE" "$ROOT/scripts/linux"
fi

SRC="$OUT/src-$NAAM"
if [ "$REF" = werkboom ]; then
  # De werkboom zoals hij is, met ongecommitte wijzigingen (bijv. een plugin in ontwikkeling).
  mkdir -p "$SRC"
  rsync -a --delete --exclude='/build*/' --exclude='/cache/' "$REPO/" "$SRC/"
  REV="$(git -C "$REPO" rev-parse --short HEAD)"
  [ -z "$(git -C "$REPO" status --porcelain --untracked-files=no)" ] || REV="$REV+wijzigingen"
else
  # Een gedeelde kloon op de gevraagde ref; submodules uit de lokale bron, niet van GitHub.
  if [ -d "$SRC/.git" ]; then
    git -C "$SRC" fetch -q origin
  else
    rm -rf "$SRC"
    git clone -q --shared --no-checkout "$REPO" "$SRC"
  fi
  git -C "$SRC" checkout -q --detach "$(git -C "$REPO" rev-parse "$REF^{commit}")"
  if [ -f "$SRC/.gitmodules" ]; then
    git -C "$SRC" submodule init -q
    git -C "$SRC" config --file .gitmodules --get-regexp '\.path$' | while read -r key pad; do
      naam="${key#submodule.}"; naam="${naam%.path}"
      if [ -e "$REPO/$pad/.git" ]; then
        git -C "$SRC" config "submodule.$naam.url" "$REPO/$pad"
      else
        # Niet uitgecheckt in de bron (bijv. flatpak/): overslaan, niet van GitHub halen.
        git -C "$SRC" config "submodule.$naam.update" none
      fi
    done
    git -C "$SRC" -c protocol.file.allow=always submodule update -q --recursive
  fi
  REV="$(git -C "$SRC" rev-parse --short HEAD)"
fi
echo "== $SOORT $NAAM: $REPO $REF ($REV)"

if [ "$SOORT" = opencpn ]; then
  BOOM="$NAAM"
  BLD="/out/$NAAM/build"
  PREFIX="${PREFIX:-${NAVPI_TESTDIR:+$NAVPI_TESTDIR/$NAAM}}"
  PREFIX="${PREFIX:-/usr/local}"
  rm -rf "$OUT/$NAAM/install" "$OUT/$NAAM/userplugins"
else
  BOOM="${CORE:-master}"
  [ -f "$OUT/$BOOM/PREFIX" ] || { echo "eerst OpenCPN '$BOOM' bouwen" >&2; exit 1; }
  PREFIX="$(cat "$OUT/$BOOM/PREFIX")"
  BLD="/out/plugin-$NAAM/build"
fi
INST="/out/$BOOM/install"
[ "${KEEP_BUILD:-0}" = 1 ] || rm -rf "$OUT/${BLD#/out/}"

docker volume create ocpn-ccache >/dev/null
docker run --rm --platform linux/arm64 \
  -v "$SRC":/src -v "$OUT":/out -v ocpn-ccache:/ccache -v "$ROOT/upstream":"$ROOT/upstream":ro -v "$REPO":"$REPO":ro \
  "$IMAGE" bash -euo pipefail -c "
    git config --global --add safe.directory '*'
    mkdir -p $BLD && cd $BLD
    cmake -G Ninja /src -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DCMAKE_INSTALL_PREFIX=$PREFIX \
      -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
      \$( [ $SOORT = opencpn ] && echo -DOCPN_BUNDLE_DOCS=OFF -DOCPN_BUNDLE_TCDATA=ON -DOCPN_BUNDLE_GSHHS=ON -DOCPN_BUILD_TEST=OFF )
    ninja -j$JOBS
    if [ -d app ] || grep -q tarball build.ninja; then
      # Plugin uit de OpenCPN-plugintemplate: installeert in zijn eigen app/files en maakt
      # een tarball voor de pluginbeheerder. De inhoud van app/files gaat in de boom.
      rm -f *.tar.gz
      ninja tarball
      mkdir -p /out/$BOOM/userplugins
      tar -C /out/$BOOM/userplugins --strip-components=1 --exclude=metadata.xml -xzf *.tar.gz
      mkdir -p /out/plugin-tarballs && cp *.tar.gz /out/plugin-tarballs/
    elif [ $SOORT = plugin ]; then
      rm -rf /tmp/plug && DESTDIR=/tmp/plug ninja install
      mkdir -p /out/$BOOM/userplugins && cp -a /tmp/plug$PREFIX/. /out/$BOOM/userplugins/
    else
      DESTDIR=$INST ninja install
    fi
    # Debug-info eruit (STRIP=0 houdt hem): scheelt honderden MB op de navpi.
    if [ ${STRIP:-1} = 1 ]; then
      mkdir -p /out/$BOOM/userplugins
      find $INST$PREFIX /out/$BOOM/userplugins -type f \( -name '*.so*' -o -path '*/bin/*' \) -exec sh -c \
        'file -b \"\$1\" | grep -q ELF && strip --strip-debug \"\$1\"' _ {} \;
    fi
  "

# Tarball van de installatieboom; de bibliotheken die op de navpi ontbreken komen er bij
# het testen bij (scripts/navpi-test.sh), niet hier.
echo "$PREFIX" > "$OUT/$BOOM/PREFIX"
COPYFILE_DISABLE=1 tar --no-xattrs -C "$OUT/$BOOM/install$PREFIX" -czf "$OUT/$BOOM.tar.gz" .
if [ -d "$OUT/$BOOM/userplugins" ]; then
  COPYFILE_DISABLE=1 tar --no-xattrs -C "$OUT/$BOOM/userplugins" -czf "$OUT/$BOOM-plugins.tar.gz" .
fi
# Een OpenCPN-build begint een nieuwe bouwlog; plugins voegen een regel toe.
[ "$SOORT" = opencpn ] && : > "$OUT/$BOOM.bouwlog"
echo "$SOORT $NAAM: $REPO $REF $REV $(date -u +%FT%TZ)" >> "$OUT/$BOOM.bouwlog"
echo "== klaar: $OUT/$BOOM.tar.gz ($SOORT $NAAM $REV, prefix $PREFIX)"
