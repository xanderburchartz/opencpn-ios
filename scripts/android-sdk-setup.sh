#!/bin/bash
# Android stap B, eenmalig: JDK 17 en de Android SDK op de Mac.
#   ~/Library/Android/jdk-17    Temurin 17 (aarch64)
#   ~/Library/Android/sdk       cmdline-tools, platform-tools, platform 34, build-tools 35,
#                               emulator en het systeemimage android-34 default arm64-v8a
# Gebruik:
#   scripts/android-sdk-setup.sh tools      # alleen JDK en cmdline-tools (geen licentie nodig)
#   scripts/android-sdk-setup.sh pakketten  # sdkmanager-pakketten; vraagt de SDK-licentie,
#                                           # die je zelf accepteert (niet automatisch)
#   scripts/android-sdk-setup.sh avd        # virtueel toestel "ocpn-test" maken
# Omgeving voor andere scripts: source scripts/android-env.sh
set -euo pipefail

ANDROID_DIR="$HOME/Library/Android"
SDK="$ANDROID_DIR/sdk"
JDK="$ANDROID_DIR/jdk-17"
DL="$ANDROID_DIR/downloads"
CMDLINE_ZIP=commandlinetools-mac-13114758_latest.zip
JDK_URL=https://github.com/adoptium/temurin17-binaries/releases/download/jdk-17.0.21.1%2B2/OpenJDK17U-jdk_aarch64_mac_hotspot_17.0.21.1_2.tar.gz
JDK_SHA=2a88ccb044a8fb00957be93245da6357c11b55717511509d147cbcc5d93b2109
PAKKETTEN=("platform-tools" "platforms;android-34" "build-tools;35.0.0" "emulator"
           "system-images;android-34;default;arm64-v8a")

case "${1:-}" in
tools)
  mkdir -p "$DL"
  if [ ! -x "$JDK/Contents/Home/bin/java" ]; then
    curl -fL --retry 3 -o "$DL/jdk17.tar.gz" "$JDK_URL"
    echo "$JDK_SHA  $DL/jdk17.tar.gz" | shasum -a 256 -c -
    mkdir -p "$JDK"
    tar -C "$JDK" --strip-components=1 -xzf "$DL/jdk17.tar.gz"
  fi
  if [ ! -x "$SDK/cmdline-tools/latest/bin/sdkmanager" ]; then
    curl -fL --retry 3 -o "$DL/$CMDLINE_ZIP" "https://dl.google.com/android/repository/$CMDLINE_ZIP"
    mkdir -p "$SDK/cmdline-tools"
    rm -rf "$SDK/cmdline-tools/.uitpak"
    unzip -q "$DL/$CMDLINE_ZIP" -d "$SDK/cmdline-tools/.uitpak"
    mv "$SDK/cmdline-tools/.uitpak/cmdline-tools" "$SDK/cmdline-tools/latest"
    rmdir "$SDK/cmdline-tools/.uitpak"
  fi
  "$JDK/Contents/Home/bin/java" -version
  JAVA_HOME="$JDK/Contents/Home" "$SDK/cmdline-tools/latest/bin/sdkmanager" --version
  ;;
pakketten)
  JAVA_HOME="$JDK/Contents/Home" "$SDK/cmdline-tools/latest/bin/sdkmanager" \
    --sdk_root="$SDK" "${PAKKETTEN[@]}"
  ;;
avd)
  JAVA_HOME="$JDK/Contents/Home" "$SDK/cmdline-tools/latest/bin/avdmanager" create avd \
    -n ocpn-test -k "system-images;android-34;default;arm64-v8a" -d pixel_tablet
  ;;
*) sed -n 2,12p "$0"; exit 2 ;;
esac
