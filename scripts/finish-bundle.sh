#!/usr/bin/env bash
# Maakt de door Ninja gebouwde app-bundel af zoals Xcode dat doet, nodig voor TestFlight en
# App Store Connect (docs/testflight.md §2). Wordt na elke bouw aangeroepen vanuit
# app/CMakeLists.txt (POST_BUILD); ook los te draaien op een bestaande bundel.
#
# Gebruik: scripts/finish-bundle.sh <opencpn.app> <iphoneos|iphonesimulator> <versie> [buildnummer]
#   <versie>        CFBundleShortVersionString, de OpenCPN-versie (bijv. 5.14.2)
#   [buildnummer]   CFBundleVersion; per upload hoger (standaard 1). Komt uit OCPNIOS_BUILD_NUMBER.
#
# Doet:
#   - versie en buildnummer;
#   - MinimumOSVersion, CFBundleSupportedPlatforms en de DT*-sleutels (SDK, Xcode, compiler);
#   - LaunchScreen.storyboard compileren tot LaunchScreen.storyboardc (ibtool);
#   - PrivacyInfo.xcprivacy (geen tracking, geen verzamelde gegevens, redenen voor de
#     API's met verplichte reden);
#   - toestemmingsteksten: Engels in Info.plist, Nederlands in nl.lproj/InfoPlist.strings.
set -euo pipefail

APP="${1:?pad naar opencpn.app}"
PLATFORM="${2:?iphoneos of iphonesimulator}"
VERSION="${3:?versie}"
BUILD="${4:-1}"
PLIST="$APP/Info.plist"
PB=/usr/libexec/PlistBuddy
[ -f "$PLIST" ] || { echo "ontbreekt: $PLIST" >&2; exit 1; }

setkey() {  # setkey <sleutel> <type> <waarde>
  "$PB" -c "Delete :$1" "$PLIST" 2>/dev/null || true
  "$PB" -c "Add :$1 $2 $3" "$PLIST"
}

# Versie en buildnummer.
setkey CFBundleShortVersionString string "$VERSION"
setkey CFBundleVersion string "$BUILD"

# Wat Xcode zelf in elke bundel zet. Zonder deze sleutels weigert App Store Connect de upload.
SDKVER="$(xcrun --sdk "$PLATFORM" --show-sdk-version)"
SDKBUILD="$(xcrun --sdk "$PLATFORM" --show-sdk-build-version)"
XCVER="$(xcodebuild -version | awk '/^Xcode/ {print $2}')"
XCBUILD="$(xcodebuild -version | awk '/^Build version/ {print $3}')"
# DTXcode is de versie zonder punten, aangevuld tot vier cijfers (26.6 -> 2660).
XCNUM="$(printf '%s' "$XCVER" | awk -F. '{printf "%d%d%d", $1, $2, ($3 == "" ? 0 : $3)}')"
if [ "$PLATFORM" = iphoneos ]; then PLATNAME=iPhoneOS; else PLATNAME=iPhoneSimulator; fi
setkey MinimumOSVersion string 18.0
# Exportregels (besluit Xander 24 nov 2026): OpenSSL voor HTTPS, standaardalgoritmen die niet
# van iOS komen, geen eigen versleuteling. ITSAppUsesNonExemptEncryption = YES vraagt bij de
# upload ook ITSEncryptionExportComplianceCode uit goedgekeurde documentatie in App Store
# Connect (upload geweigerd zonder, 24 nov). Daarom de sleutel weglaten: App Store Connect
# vraagt het dan per build en daar wordt het beantwoord.
"$PB" -c "Delete :ITSAppUsesNonExemptEncryption" "$PLIST" 2>/dev/null || true
# Alleen 64-bit (arm64); App Store Connect weigert de upload zonder deze sleutel.
"$PB" -c "Delete :UIRequiredDeviceCapabilities" "$PLIST" 2>/dev/null || true
"$PB" -c "Add :UIRequiredDeviceCapabilities array" "$PLIST"
"$PB" -c "Add :UIRequiredDeviceCapabilities:0 string arm64" "$PLIST"
"$PB" -c "Delete :CFBundleSupportedPlatforms" "$PLIST" 2>/dev/null || true
"$PB" -c "Add :CFBundleSupportedPlatforms array" "$PLIST"
"$PB" -c "Add :CFBundleSupportedPlatforms:0 string $PLATNAME" "$PLIST"
setkey DTPlatformName string "$PLATFORM"
setkey DTPlatformVersion string "$SDKVER"
setkey DTPlatformBuild string "$SDKBUILD"
setkey DTSDKName string "$PLATFORM$SDKVER"
setkey DTSDKBuild string "$SDKBUILD"
setkey DTXcode string "$XCNUM"
setkey DTXcodeBuild string "$XCBUILD"
setkey DTCompiler string com.apple.compilers.llvm.clang.1_0
setkey BuildMachineOSBuild string "$(sw_vers -buildVersion)"
"$PB" -c "Delete :NOTE" "$PLIST" 2>/dev/null || true

# Opstartscherm: Qt zet het ongecompileerde storyboard in de bundel; iOS gebruikt de
# gecompileerde vorm.
if [ -f "$APP/LaunchScreen.storyboard" ]; then
  xcrun ibtool --errors --warnings --output-format human-readable-text \
    --minimum-deployment-target 18.0 --target-device iphone --target-device ipad \
    --compile "$APP/LaunchScreen.storyboardc" "$APP/LaunchScreen.storyboard" >/dev/null
  rm -f "$APP/LaunchScreen.storyboard"
fi

# Toestemmingsteksten: Engels in Info.plist (ontwikkeltaal en), Nederlands apart.
setkey NSLocalNetworkUsageDescription string \
  "'OpenCPN reads NMEA and AIS data from instruments on the local network.'"
setkey NSLocationWhenInUseUsageDescription string \
  "'OpenCPN uses the GPS of this device as a position source when the Built-in GPS connection is on.'"
setkey NSMotionUsageDescription string \
  "'OpenCPN reads the barometer of this device for air pressure.'"
# Bonjour-diensten die de app zoekt (mayara_pi via dns_sd: mayara-server en Signal K).
"$PB" -c "Delete :NSBonjourServices" "$PLIST" 2>/dev/null || true
"$PB" -c "Add :NSBonjourServices array" "$PLIST"
"$PB" -c "Add :NSBonjourServices:0 string _mayara-http._tcp" "$PLIST"
"$PB" -c "Add :NSBonjourServices:1 string _signalk-http._tcp" "$PLIST"
mkdir -p "$APP/nl.lproj"
cat > "$APP/nl.lproj/InfoPlist.strings" <<'EOF'
"NSLocalNetworkUsageDescription" = "OpenCPN leest NMEA- en AIS-gegevens van instrumenten op het lokale netwerk.";
"NSLocationWhenInUseUsageDescription" = "OpenCPN gebruikt de GPS van dit toestel als positiebron wanneer de verbinding Ingebouwde GPS aan staat.";
"NSMotionUsageDescription" = "OpenCPN leest de barometer van dit toestel voor de luchtdruk.";
EOF

# Privacymanifest. De app verzamelt niets en volgt niemand. Redenen voor API's met
# verplichte reden (gemeten met nm -u, 18 nov 2026):
#   C617.1 bestandstijden van bestanden in de eigen container (stat/fstat/lstat);
#   E174.1 vrije ruimte controleren voor het schrijven (statfs/statvfs/fstatfs);
#   CA92.1 eigen instellingen in UserDefaults (Qt).
cat > "$APP/PrivacyInfo.xcprivacy" <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>NSPrivacyTracking</key>
	<false/>
	<key>NSPrivacyTrackingDomains</key>
	<array/>
	<key>NSPrivacyCollectedDataTypes</key>
	<array/>
	<key>NSPrivacyAccessedAPITypes</key>
	<array>
		<dict>
			<key>NSPrivacyAccessedAPIType</key>
			<string>NSPrivacyAccessedAPICategoryFileTimestamp</string>
			<key>NSPrivacyAccessedAPITypeReasons</key>
			<array><string>C617.1</string></array>
		</dict>
		<dict>
			<key>NSPrivacyAccessedAPIType</key>
			<string>NSPrivacyAccessedAPICategoryDiskSpace</string>
			<key>NSPrivacyAccessedAPITypeReasons</key>
			<array><string>E174.1</string></array>
		</dict>
		<dict>
			<key>NSPrivacyAccessedAPIType</key>
			<string>NSPrivacyAccessedAPICategoryUserDefaults</string>
			<key>NSPrivacyAccessedAPITypeReasons</key>
			<array><string>CA92.1</string></array>
		</dict>
	</array>
</dict>
</plist>
EOF
plutil -lint "$PLIST" "$APP/PrivacyInfo.xcprivacy" "$APP/nl.lproj/InfoPlist.strings" >/dev/null
