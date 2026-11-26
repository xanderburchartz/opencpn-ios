#!/usr/bin/env bash
# Pakt de Release-bouw in als Xcode-archief en exporteert die voor App Store Connect, zoals
# bij Triton (docs/testflight.md). Ondertekenen doet Xcode (automatisch, met het aangemelde
# account en het Apple Distribution-certificaat); er staat geen sleutel in deze repo.
#
# Gebruik: OCPNIOS_TEAM_ID=<team> scripts/package-testflight.sh [export|upload]
#   export  (standaard) maakt build/testflight/export/opencpn.ipa; uploadt niets.
#   upload  uploadt naar App Store Connect (TestFlight). Alleen na akkoord per build, en
#           pas als het app-record bestaat.
# Vooraf: OCPNIOS_BUILD_NUMBER=<n> scripts/build-ocpn-ios.sh device-release all
# (buildnummer per upload hoger dan het vorige).
#
# Aanmelden bij App Store Connect, een van beide:
#   - het Xcode-account (Xcode › Settings › Accounts). Werkt niet als het scherm van de Mac op slot
#     staat: xcodebuild leest de aanmelding dan niet uit de sleutelhanger ("missing Xcode-Username")
#     en Xcode raakt het account kwijt (gemeten 24-25 nov 2026, logboek);
#   - een App Store Connect API-sleutel: OCPNIOS_ASC_KEY_ID en OCPNIOS_ASC_ISSUER_ID, met het
#     bestand ~/.appstoreconnect/private_keys/AuthKey_<KEY_ID>.p8 (buiten de repo; ander pad met
#     OCPNIOS_ASC_KEY_PATH). Hangt niet aan de Xcode-aanmelding.
# Externe test: standaard mag de build ook naar externe groepen. OCPNIOS_TF_INTERNAL_ONLY=1 maakt
# hem alleen intern (testFlightInternalTestingOnly; zo'n build kan nooit naar een externe groep).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
: "${OCPNIOS_TEAM_ID:?zet OCPNIOS_TEAM_ID}"
MODE="${1:-export}"
case "$MODE" in export|upload) ;; *) echo "export of upload" >&2; exit 1 ;; esac
SFX="${OCPNIOS_BUILD_SUFFIX:-}"
SRC="$ROOT/build/device-release/ocpn$SFX/opencpn.app"
OUT="$ROOT/build/testflight$SFX"
[ -d "$SRC" ] || { echo "ontbreekt: $SRC (eerst device-release bouwen)" >&2; exit 1; }

PB=/usr/libexec/PlistBuddy
BID="$("$PB" -c 'Print :CFBundleIdentifier' "$SRC/Info.plist")"
VER="$("$PB" -c 'Print :CFBundleShortVersionString' "$SRC/Info.plist")"
BUILD="$("$PB" -c 'Print :CFBundleVersion' "$SRC/Info.plist")"
NAME="$("$PB" -c 'Print :CFBundleName' "$SRC/Info.plist")"
echo "app $BID $VER ($BUILD)"

# Een archief zoals Xcode het maakt: Products/Applications/<app> plus Info.plist.
ARCH="$OUT/OpenCPN.xcarchive"
rm -rf "$OUT"
mkdir -p "$ARCH/Products/Applications" "$ARCH/dSYMs"
rsync -a "$SRC/" "$ARCH/Products/Applications/opencpn.app/"
# Eventuele ontwikkelondertekening en profiel eruit; Xcode tekent opnieuw.
rm -f "$ARCH/Products/Applications/opencpn.app/embedded.mobileprovision"
# Entitlements meegeven zoals een Xcode-archief dat doet: de export neemt ze over uit de
# handtekening in het archief (alleen wat de App ID toestaat). Multicast: UDP-broadcast en
# -multicast (NMEA over UDP, mDNS voor SignalK), toegekend aan het team, aangezet 24 nov 2026.
codesign --force --sign - --entitlements "$ROOT/app/ios/opencpn.entitlements" \
  --generate-entitlement-der "$ARCH/Products/Applications/opencpn.app"
cat > "$ARCH/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>ApplicationProperties</key>
	<dict>
		<key>ApplicationPath</key>
		<string>Applications/opencpn.app</string>
		<key>Architectures</key>
		<array><string>arm64</string></array>
		<key>CFBundleIdentifier</key>
		<string>$BID</string>
		<key>CFBundleShortVersionString</key>
		<string>$VER</string>
		<key>CFBundleVersion</key>
		<string>$BUILD</string>
		<key>Team</key>
		<string>$OCPNIOS_TEAM_ID</string>
	</dict>
	<key>ArchiveVersion</key>
	<integer>2</integer>
	<key>CreationDate</key>
	<date>$(date -u +%Y-%m-%dT%H:%M:%SZ)</date>
	<key>Name</key>
	<string>$NAME</string>
	<key>SchemeName</key>
	<string>opencpn</string>
</dict>
</plist>
EOF

DEST=export
[ "$MODE" = upload ] && DEST=upload
INTERNAL_ONLY=false
[ "${OCPNIOS_TF_INTERNAL_ONLY:-0}" = 1 ] && INTERNAL_ONLY=true
AUTH=()
if [ -n "${OCPNIOS_ASC_KEY_ID:-}" ]; then
  : "${OCPNIOS_ASC_ISSUER_ID:?zet OCPNIOS_ASC_ISSUER_ID bij OCPNIOS_ASC_KEY_ID}"
  KEY="${OCPNIOS_ASC_KEY_PATH:-$HOME/.appstoreconnect/private_keys/AuthKey_$OCPNIOS_ASC_KEY_ID.p8}"
  [ -f "$KEY" ] || { echo "ontbreekt: $KEY" >&2; exit 1; }
  AUTH=(-authenticationKeyPath "$KEY" -authenticationKeyID "$OCPNIOS_ASC_KEY_ID"
        -authenticationKeyIssuerID "$OCPNIOS_ASC_ISSUER_ID")
fi
cat > "$OUT/ExportOptions.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>method</key>
	<string>app-store-connect</string>
	<key>destination</key>
	<string>$DEST</string>
	<key>signingStyle</key>
	<string>automatic</string>
	<key>teamID</key>
	<string>$OCPNIOS_TEAM_ID</string>
	<key>uploadSymbols</key>
	<false/>
	<key>manageAppVersionAndBuildNumber</key>
	<false/>
	<key>testFlightInternalTestingOnly</key>
	<$INTERNAL_ONLY/>
</dict>
</plist>
EOF

xcodebuild -exportArchive -archivePath "$ARCH" -exportOptionsPlist "$OUT/ExportOptions.plist" \
  -exportPath "$OUT/export" -allowProvisioningUpdates ${AUTH[@]+"${AUTH[@]}"}
[ "$MODE" = export ] && ls -la "$OUT/export"; exit 0
