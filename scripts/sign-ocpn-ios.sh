#!/usr/bin/env bash
# Ondertekent build/device/ocpn/opencpn.app voor een apparaat (Personal Team, 7 dagen geldig).
#
# Gebruik: OCPNIOS_TEAM_ID=<team> scripts/sign-ocpn-ios.sh [profiel.mobileprovision]
#          OCPNIOS_BUILD=device-release ...   tekent de Release-bouw (standaard: device)
#
# De Ninja-bouw tekent niet. Een profiel voor de bundel-ID haal je met een klein Xcode-
# stubproject met dezelfde bundel-ID en -allowProvisioningUpdates (zie docs/logboek.md,
# fase 4). Zonder argument zoekt het script in build/device/profiel-stub.
# Uitvoer: build/device/signed/opencpn.app (kopie; de bouwmap blijft ongetekend).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
: "${OCPNIOS_TEAM_ID:?zet OCPNIOS_TEAM_ID}"
BUILD="${OCPNIOS_BUILD:-device}"
SRC="$ROOT/build/$BUILD/ocpn/opencpn.app"
OUTDIR="$ROOT/build/$BUILD/signed"
APP="$OUTDIR/opencpn.app"
# Personal Team: Xcode maakt per toestel een eigen profiel. Bewaard in
# build/device/profielen/ (iphone.mobileprovision, ipad.mobileprovision); standaard iPhone.
PROFILE="${1:-${OCPNIOS_PROFILE:-$ROOT/build/device/profielen/iphone.mobileprovision}}"
[ -d "$SRC" ] || { echo "ontbreekt: $SRC" >&2; exit 1; }
[ -f "$PROFILE" ] || { echo "ontbreekt: profiel $PROFILE" >&2; exit 1; }

mkdir -p "$OUTDIR"
rsync -a --delete "$SRC/" "$APP/"
cp "$PROFILE" "$APP/embedded.mobileprovision"

PB=/usr/libexec/PlistBuddy
"$PB" -c "Set :CFBundleDisplayName OpenCPN" "$APP/Info.plist"
"$PB" -c "Delete :NSLocalNetworkUsageDescription" "$APP/Info.plist" 2>/dev/null || true
"$PB" -c "Add :NSLocalNetworkUsageDescription string 'OpenCPN leest NMEA- en AIS-gegevens van instrumenten op het lokale netwerk.'" "$APP/Info.plist"

# Rechten uit het profiel halen
ENT="$OUTDIR/entitlements.plist"
security cms -D -i "$APP/embedded.mobileprovision" > "$OUTDIR/profiel.plist"
plutil -extract Entitlements xml1 -o "$ENT" "$OUTDIR/profiel.plist"

IDENT="$(security find-identity -v -p codesigning | awk '/Apple Development/ {print $2; exit}')"
[ -n "$IDENT" ] || { echo "geen Apple Development-identiteit gevonden" >&2; exit 1; }
codesign --force --sign "$IDENT" --entitlements "$ENT" --timestamp=none --generate-entitlement-der "$APP"
codesign --verify --strict "$APP" && echo "ondertekend: $APP"
