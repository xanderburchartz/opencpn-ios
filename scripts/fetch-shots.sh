#!/bin/zsh
# Schermafbeeldingen ophalen die de app zelf maakt (app/ios/screen_capture.mm).
#
# Gebruik: scripts/fetch-shots.sh <toestel> [doelmap]
#   <toestel>  devicectl-id of naam van het toestel (zie `xcrun devicectl list devices`),
#              of "sim:<udid>" voor een simulator.
#   [doelmap]  standaard build/shots/<toestel>/<tijd>
# De app bewaart de laatste 40 afbeeldingen in Documents/log/schermafbeeldingen.
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail

DEV="${1:?gebruik: scripts/fetch-shots.sh <toestel> [doelmap]}"
APP_ID="local.opencpnios.opencpn"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SAFE="${DEV//[^A-Za-z0-9_-]/_}"
DEST="${2:-$ROOT/build/shots/$SAFE/$(date +%Y%m%d-%H%M%S)}"
mkdir -p "$DEST"

if [[ "$DEV" == sim:* ]]; then
  UDID="${DEV#sim:}"
  DATA="$(xcrun simctl get_app_container "$UDID" "$APP_ID" data)"
  cp "$DATA/Documents/log/schermafbeeldingen/"*.jpg "$DEST/" 2>/dev/null || true
else
  if ! xcrun devicectl device copy from --device "$DEV" \
      --domain-type appDataContainer --domain-identifier "$APP_ID" \
      --source Documents/log/schermafbeeldingen --destination "$DEST" >/dev/null 2>"$DEST/.fout"; then
    echo "Niets opgehaald. Is de app sinds de installatie al gestart en is er iets geopend of"
    echo "getikt? De map Documents/log/schermafbeeldingen bestaat pas na de eerste opname."
    sed 's/^/  /' "$DEST/.fout"; rm -f "$DEST/.fout"; exit 1
  fi
  rm -f "$DEST/.fout"
  # devicectl zet de map zelf in de bestemming; plat maken.
  if [[ -d "$DEST/schermafbeeldingen" ]]; then
    mv "$DEST/schermafbeeldingen/"* "$DEST/" 2>/dev/null || true
    rmdir "$DEST/schermafbeeldingen" 2>/dev/null || true
  fi
fi

N=$(ls "$DEST"/*.jpg 2>/dev/null | wc -l | tr -d ' ')
echo "$N afbeeldingen in $DEST"
