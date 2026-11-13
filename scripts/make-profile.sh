#!/usr/bin/env bash
# Haalt een ontwikkelprofiel (mobileprovision) voor de app op, via een leeg Xcode-project met
# dezelfde bundel-ID. De Ninja-bouw tekent niet; scripts/sign-ocpn-ios.sh heeft dit profiel nodig.
#
# Gebruik: OCPNIOS_TEAM_ID=<team> [OCPNIOS_BUNDLE_PREFIX=<voorvoegsel>] scripts/make-profile.sh <naam>
#   <naam>  bestandsnaam zonder extensie in build/device/profielen/ (bijv. iphone of ipad)
# Vooraf: het toestel is met de Mac verbonden (kabel of hetzelfde netwerk), staat in
# ontwikkelaarsmodus en is in Xcode bij je Apple-ID (Personal Team) geregistreerd.
# Vereist: xcodegen (brew install xcodegen).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
: "${OCPNIOS_TEAM_ID:?zet OCPNIOS_TEAM_ID (je Team-ID, zie Xcode > Settings > Accounts)}"
NAME="${1:?geef een naam voor het profiel, bijv. iphone}"
PREFIX="${OCPNIOS_BUNDLE_PREFIX:-local.opencpnios}"
BUNDLE_ID="$PREFIX.opencpn"

STUB="$ROOT/build/device/profiel-stub"
OUT="$ROOT/build/device/profielen"
mkdir -p "$STUB/src" "$OUT"
cp "$ROOT/scripts/tools/profiel-stub/src/main.m" "$STUB/src/main.m"
cat > "$STUB/project.yml" <<YML
name: ProfielStub
options:
  deploymentTarget: {iOS: "18.0"}
targets:
  ProfielStub:
    type: application
    platform: iOS
    sources: [src]
    settings:
      PRODUCT_BUNDLE_IDENTIFIER: $BUNDLE_ID
      DEVELOPMENT_TEAM: $OCPNIOS_TEAM_ID
      CODE_SIGN_STYLE: Automatic
      GENERATE_INFOPLIST_FILE: YES
YML
(cd "$STUB" && xcodegen generate >/dev/null && xcodebuild -project ProfielStub.xcodeproj \
  -scheme ProfielStub -destination 'generic/platform=iOS' -allowProvisioningUpdates \
  -derivedDataPath dd build >"$ROOT/build/logs/profiel-$NAME.log" 2>&1) \
  || { echo "xcodebuild mislukt, zie build/logs/profiel-$NAME.log" >&2; exit 1; }
cp "$STUB/dd/Build/Products/Debug-iphoneos/ProfielStub.app/embedded.mobileprovision" "$OUT/$NAME.mobileprovision"
echo "profiel: $OUT/$NAME.mobileprovision (bundel-ID $BUNDLE_ID)"
