#!/usr/bin/env bash
# Maakt Assets.car met het app-pictogram uit OpenCPN's eigen Mac-pictogram
# (upstream/OpenCPN/buildosx/opencpn.icns), zoals Xcode dat met een asset catalog doet.
# Aangeroepen vanuit app/CMakeLists.txt na het bouwen.
# Gebruik: make-app-icon.sh <icns> <werkmap> <app-bundel> <iphoneos|iphonesimulator>
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail
ICNS="$1"; WORK="$2"; APP="$3"; PLATFORM="$4"

CAT="$WORK/Assets.xcassets"
SET="$CAT/AppIcon.appiconset"
mkdir -p "$SET"
# 1024 px, zonder de zwarte rand van het Mac-pictogram (iOS rondt zelf af) en zonder
# alfakanaal (iOS-eis voor app-pictogrammen).
sips -s format png "$ICNS" --out "$WORK/icon-src.png" >/dev/null
sips --cropToHeightWidth 1000 1000 "$WORK/icon-src.png" --out "$WORK/icon-crop.png" >/dev/null
sips -z 1024 1024 "$WORK/icon-crop.png" --out "$WORK/icon-1024.png" >/dev/null
sips -s format jpeg "$WORK/icon-1024.png" --out "$WORK/icon.jpg" >/dev/null
sips -s format png "$WORK/icon.jpg" --out "$SET/icon-1024.png" >/dev/null

cat > "$CAT/Contents.json" <<'EOF'
{ "info" : { "author" : "xcode", "version" : 1 } }
EOF
cat > "$SET/Contents.json" <<'EOF'
{
  "images" : [
    { "filename" : "icon-1024.png", "idiom" : "universal", "platform" : "ios", "size" : "1024x1024" }
  ],
  "info" : { "author" : "xcode", "version" : 1 }
}
EOF

xcrun actool "$CAT" --compile "$APP" --platform "$PLATFORM" \
  --minimum-deployment-target 17.0 --app-icon AppIcon \
  --target-device iphone --target-device ipad \
  --output-partial-info-plist "$WORK/icon-info.plist" --errors --warnings >/dev/null

# Pictogramsleutels (CFBundleIcons, CFBundleIcons~ipad) in de Info.plist van de app.
/usr/libexec/PlistBuddy -c "Delete :CFBundleIconFile" "$APP/Info.plist" 2>/dev/null || true
for key in CFBundleIcons "CFBundleIcons~ipad"; do
  /usr/libexec/PlistBuddy -c "Delete :$key" "$APP/Info.plist" 2>/dev/null || true
done
/usr/libexec/PlistBuddy -c "Merge $WORK/icon-info.plist" "$APP/Info.plist"
