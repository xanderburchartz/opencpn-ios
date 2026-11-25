#!/bin/bash
# Zet een Linux-build (scripts/build-linux.sh) als losse test-OpenCPN op de navpi en start
# of controleert hem. Alles komt in $NAVPI_TESTDIR/<naam> op de navpi, met een eigen HOME; de flatpak-
# OpenCPN, zijn instellingen en de diensten worden niet aangeraakt. Er wordt niets met apt
# geïnstalleerd: bibliotheken die op de navpi ontbreken gaan mee in <naam>/lib/bundle.
#
# Gebruik:
#   scripts/navpi-test.sh zet   <naam>   # bundelen en kopiëren naar ~/ocpn-test/<naam>
#   scripts/navpi-test.sh check <naam>   # ontbrekende bibliotheken en versie, zonder venster
#   scripts/navpi-test.sh start <naam>   # venster op het scherm aan boord (labwc, wayland-0)
#   scripts/navpi-test.sh stop  <naam>   # alleen de eigen test-OpenCPN stoppen (pid-bestand)
#   scripts/navpi-test.sh log   <naam>   # laatste regels van opencpn.log
# Instelling (geen adressen in code): ~/.config/opencpn-ios/navpi.env met
#   NAVPI=<gebruiker>@<adres>      ssh-doel
#   NAVPI_TESTDIR=<absoluut pad>   testmap op de navpi, ook de bouwprefix (build-linux.sh)
#   NAVPI_KEY=<pad>                ssh-sleutel (optioneel)
#
# Start alleen na akkoord via de regie, nooit tijdens het varen, en nooit tegelijk met een
# andere OpenCPN die met de YDNR verbonden is. Een verse test-HOME heeft geen verbindingen.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CONF="$HOME/.config/opencpn-ios/navpi.env"
# shellcheck disable=SC1090
[ -f "$CONF" ] && . "$CONF"
: "${NAVPI:?NAVPI ontbreekt, zie $CONF}" "${NAVPI_TESTDIR:?NAVPI_TESTDIR ontbreekt, zie $CONF}"
KEY="${NAVPI_KEY:-$HOME/.ssh/id_ed25519}"
IMAGE="opencpn-ios/linux-bouw:trixie"
ACTIE="${1:-}"; NAAM="${2:-}"
case "$NAAM" in ''|*/*|.*) sed -n 2,18p "$0"; exit 2 ;; esac
pi() { ssh -i "$KEY" -o BatchMode=yes -o ConnectTimeout=8 "$NAVPI" "$@"; }
DOEL="$NAVPI_TESTDIR/$NAAM"

case "$ACTIE" in
zet)
  TGZ="$ROOT/build/linux/$NAAM.tar.gz"
  [ -f "$TGZ" ] || { echo "geen $TGZ; eerst scripts/build-linux.sh" >&2; exit 1; }
  [ "$(cat "$ROOT/build/linux/$NAAM/PREFIX")" = "$DOEL" ] || {
    echo "gebouwd met prefix $(cat "$ROOT/build/linux/$NAAM/PREFIX"), niet $DOEL; opnieuw bouwen" >&2; exit 1; }
  STAGE="$ROOT/build/linux/stage-$NAAM"
  rm -rf "$STAGE"; mkdir -p "$STAGE"
  tar -C "$STAGE" -xzf "$TGZ"
  PTGZ="$ROOT/build/linux/$NAAM-plugins.tar.gz"
  mkdir -p "$STAGE/home/.local"
  [ -f "$PTGZ" ] && tar -C "$STAGE/home/.local" -xzf "$PTGZ"
  # Bibliotheken die op de navpi al staan (alleen lezen: ldconfig -p).
  pi '/sbin/ldconfig -p' | awk '/=>/{print $1}' | sort -u > "$STAGE/.navpi-libs"
  # In de container: alle afhankelijkheden van opencpn en de plugins, min wat de navpi heeft.
  docker run --rm --platform linux/arm64 -v "$STAGE":/stage "$IMAGE" bash -euo pipefail -c '
    mkdir -p /stage/lib/bundle
    for f in /stage/bin/opencpn /stage/lib/opencpn/*.so /stage/home/.local/lib/opencpn/*.so; do
      [ -f "$f" ] || continue
      LD_LIBRARY_PATH=/stage/lib:/stage/lib/opencpn:/stage/home/.local/lib/opencpn ldd "$f" | awk "/=> \//{print \$1, \$3}"
    done | sort -u | while read -r so pad; do
      case "$pad" in /stage/*) continue ;; esac
      grep -qxF "$so" /stage/.navpi-libs || cp -nL "$pad" /stage/lib/bundle/
    done
    ls /stage/lib/bundle'
  cat > "$STAGE/run.sh" <<'EOF'
#!/bin/sh
# Start de test-OpenCPN met een eigen HOME in deze map. Raakt de flatpak-OpenCPN niet.
D="$(cd "$(dirname "$0")" && pwd)"
# OpenCPN zet zelf GDK_BACKEND=x11 als WAYLAND_DISPLAY bestaat; Xwayland (:0) vraagt de
# sleutel uit de echte HOME, dus die eerst vastleggen.
export XAUTHORITY="${XAUTHORITY:-$HOME/.Xauthority}"
export HOME="$D/home" XDG_CONFIG_HOME="$D/home/.config" XDG_DATA_HOME="$D/home/.local/share" \
       XDG_CACHE_HOME="$D/home/.cache"
mkdir -p "$HOME"
export LD_LIBRARY_PATH="$D/lib/bundle:$D/lib:$D/lib/opencpn${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$D/bin/opencpn" "$@"
EOF
  chmod +x "$STAGE/run.sh"
  # Alles behalve de test-HOME vervangen; in de test-HOME alleen de plugins (.local).
  pi "mkdir -p $DOEL && find $DOEL -mindepth 1 -maxdepth 1 ! -name home -exec rm -rf {} + && \
      rm -rf $DOEL/home/.local/lib/opencpn $DOEL/home/.local/share/opencpn/plugins"
  COPYFILE_DISABLE=1 tar --no-xattrs -C "$STAGE" --exclude=.navpi-libs -czf - . | pi "tar -C $DOEL -xzf -"
  pi "du -sh $DOEL"
  pi "cat > $DOEL/BUILD-INFO" < "$ROOT/build/linux/$NAAM.bouwlog"
  # Test-HOME: zonder opencpn.conf start OpenCPN de wizard (b_initial_load in
  # gui/src/ocpn_app.cpp), en het welkomst-/licentievenster komt bij elke nieuwe
  # "Version <VERSION_FULL> Build <VERSION_DATE>". Op de navpi is geen tool om dat op afstand
  # door te klikken, dus zetten we NavMessageShown en ConfigVersionString (de licentie van
  # eigen testbuilds accepteren we zelf). Geen verbindingen: de test-OpenCPN leest en zendt niets.
  CFG="$ROOT/build/linux/$NAAM/build/include/config.h"
  VFULL="$(sed -n 's/^#define VERSION_FULL "\(.*\)"/\1/p' "$CFG")"
  VDATE="$(sed -n 's/^#define VERSION_DATE "\(.*\)"/\1/p' "$CFG")"
  VSTR="Version $VFULL Build $VDATE"
  pi "c=$DOEL/home/.opencpn/opencpn.conf; mkdir -p \$(dirname \$c); \
      [ -f \$c ] || printf '[Settings]\n' > \$c; \
      grep -q '^NavMessageShown=' \$c || sed -i '/^\[Settings\]/a NavMessageShown=1' \$c; \
      if grep -q '^ConfigVersionString=' \$c; then sed -i 's/^ConfigVersionString=.*/ConfigVersionString=$VSTR/' \$c; \
      else sed -i '/^\[Settings\]/a ConfigVersionString=$VSTR' \$c; fi; \
      grep -E '^(NavMessageShown|ConfigVersionString)=' \$c"
  echo "== op de navpi: $DOEL"
  ;;
check)
  pi "cd $DOEL && cat BUILD-INFO; \
      LD_LIBRARY_PATH=lib/bundle:lib:lib/opencpn ldd bin/opencpn | grep 'not found' || echo 'alle bibliotheken gevonden'; \
      for p in lib/opencpn/*.so home/.local/lib/opencpn/*.so; do [ -f \"\$p\" ] && { LD_LIBRARY_PATH=lib/bundle:lib:lib/opencpn:home/.local/lib/opencpn ldd \"\$p\" | grep -q 'not found' && echo \"ontbreekt bij \$p\" || echo \"ok \$p\"; }; done; \
      ./run.sh --version 2>&1 | head -5 || true"
  ;;
start)
  pi "cd $DOEL && if [ -f opencpn.pid ] && kill -0 \$(cat opencpn.pid) 2>/dev/null; then echo 'draait al'; exit 0; fi; \
      DISPLAY=:0 WAYLAND_DISPLAY=wayland-0 XDG_RUNTIME_DIR=/run/user/\$(id -u) \
      nohup ./run.sh > run.out 2>&1 & echo \$! > opencpn.pid; sleep 8; \
      kill -0 \$(cat opencpn.pid) && echo \"draait, pid \$(cat opencpn.pid)\" || { echo gestopt; tail -20 run.out; }"
  ;;
stop)
  # Eerst netjes; OpenCPN negeert SIGTERM zolang er een modaal venster open staat, dan na 5 s
  # SIGKILL. Alleen processen waarvan de binary in deze testmap staat.
  pi "cd $DOEL; for p in \$(pgrep -x opencpn); do \
        case \$(readlink /proc/\$p/exe) in $DOEL/*) ;; *) continue ;; esac; \
        kill \$p; for i in 1 2 3 4 5; do kill -0 \$p 2>/dev/null || break; sleep 1; done; \
        kill -0 \$p 2>/dev/null && kill -KILL \$p && echo \"pid \$p hard gestopt\" || echo \"pid \$p gestopt\"; \
      done; rm -f opencpn.pid; \
      pgrep -x opencpn >/dev/null || rm -f home/.opencpn/_OpenCPN_SILock home/.opencpn/opencpn-ipc home/.opencpn/startcheck.dat"
  # (Na een harde stop blijven het instance-slot en startcheck.dat staan; zonder lopende
  #  OpenCPN mogen die weg, anders weigert de volgende start of vraagt hij om veilige modus.)
  ;;
log)
  pi "tail -40 $DOEL/home/.opencpn/opencpn.log 2>/dev/null || tail -40 $DOEL/run.out"
  ;;
*) sed -n 2,18p "$0"; exit 2 ;;
esac
