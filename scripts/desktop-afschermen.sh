#!/usr/bin/env bash
# Schermt een desktop-proefbouw af van de gewone OpenCPN-installatie op deze Mac.
#
# Gebruik: scripts/desktop-afschermen.sh <pad/naar/OpenCPN.app> <thuismap>
#
# Waarom: OpenCPN op macOS deelt anders met de gewone installatie de pluginmap
# (~/Library/Application Support/OpenCPN, via $HOME), load_stamps en plugins/install_data
# (~/Library/Preferences/opencpn, via NSFileManager, negeert -c en $HOME). Een proefbouw kan
# zo plugins van de gewone installatie blokkeren of verwijderen (17 nov 2026).
#
# Wat het doet:
# - De echte binary wordt Contents/MacOS/OpenCPN.bin; Contents/MacOS/OpenCPN wordt een
#   startscript dat HOME=<thuismap> en OCPN_TEST_HOMEDIR=<thuismap>/Library/Preferences/opencpn
#   zet. Ook dubbelklikken of `open` start dus afgeschermd. Argumenten (bijv. -c) gaan mee.
# - Alleen een binary met tools/desktop-test/osx-test-homedir.patch volgt OCPN_TEST_HOMEDIR
#   (en weigert te starten zonder). Zonder die patch kan afschermen niet volledig; dan wordt
#   het startscript een weigering met uitleg.
# Na een nieuwe `ninja` staat de binary weer als Contents/MacOS/OpenCPN; met de patch weigert
# hij dan zelf te starten. Draai dit script na elke bouw opnieuw.
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail

APP="${1:?pad naar OpenCPN.app}"; THUIS="${2:?thuismap}"
MACOS="$APP/Contents/MacOS"
[ -d "$MACOS" ] || { echo "geen app: $APP" >&2; exit 1; }
mkdir -p "$THUIS"
THUIS="$(cd "$THUIS" && pwd)"
case "$THUIS" in
  "$HOME"|"$HOME/") echo "thuismap mag niet de echte thuismap zijn" >&2; exit 1 ;;
esac

# Nieuwe bouw: binary staat als OpenCPN. Anders staat hij al als OpenCPN.bin.
if file "$MACOS/OpenCPN" | grep -q 'Mach-O'; then
  mv -f "$MACOS/OpenCPN" "$MACOS/OpenCPN.bin"
fi
[ -f "$MACOS/OpenCPN.bin" ] || { echo "geen binary in $MACOS" >&2; exit 1; }

if grep -q OCPN_TEST_HOMEDIR "$MACOS/OpenCPN.bin"; then
  mkdir -p "$THUIS/Library/Preferences/opencpn" "$THUIS/Library/Logs"
  cat > "$MACOS/OpenCPN" <<EOF
#!/bin/bash
# Proefbouw: start met een eigen thuismap (scripts/desktop-afschermen.sh).
export HOME="$THUIS"
export OCPN_TEST_HOMEDIR="$THUIS/Library/Preferences/opencpn"
exec "\$(dirname "\$0")/OpenCPN.bin" "\$@"
EOF
  echo "afgeschermd: $APP -> $THUIS"
else
  cat > "$MACOS/OpenCPN" <<'EOF'
#!/bin/bash
# Proefbouw zonder osx-test-homedir.patch: niet af te schermen, start geweigerd.
msg="Deze proefbouw is niet afgeschermd van de gewone OpenCPN en start daarom niet. Bouw opnieuw met tools/desktop-test/osx-test-homedir.patch."
echo "$msg" >&2
osascript -e "display alert \"OpenCPN-proefbouw\" message \"$msg\"" >/dev/null 2>&1 || true
exit 78
EOF
  echo "geweigerd (binary zonder patch): $APP"
fi
chmod +x "$MACOS/OpenCPN"
codesign --force --deep -s - "$APP" 2>/dev/null
