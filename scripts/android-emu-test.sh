#!/bin/bash
# Test een APK uit scripts/build-android-apk.sh in de Android-emulator op de Mac
# (AVD "ocpn-test", Android 34 arm64 zonder Google-diensten; zie android-sdk-setup.sh avd).
#
# Gebruik:
#   scripts/android-emu-test.sh start            # emulator starten (venster), wachten tot klaar
#   scripts/android-emu-test.sh install <naam>   # APK van build/android/apk-<naam> installeren
#   scripts/android-emu-test.sh open             # OpenCPN starten
#   scripts/android-emu-test.sh shot [bestand]   # schermafbeelding (standaard build/android/shots/)
#   scripts/android-emu-test.sh log              # crashes en OpenCPN-regels uit logcat
#   scripts/android-emu-test.sh ydnr             # alleen-lezen-test naar de YDNR vanuit de emulator
#   scripts/android-emu-test.sh stop             # emulator afsluiten
# Instelling (geen adressen in code): YDNR=<adres> in ~/.config/opencpn-ios/navpi.env.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SDK="$HOME/Library/Android/sdk"
ADB="$SDK/platform-tools/adb"
PKG=org.opencpn.opencpn
CONF="$HOME/.config/opencpn-ios/navpi.env"
# shellcheck disable=SC1090
[ -f "$CONF" ] && . "$CONF"

case "${1:-}" in
start)
  mkdir -p "$ROOT/build/logs"
  nohup "$SDK/emulator/emulator" -avd ocpn-test -no-snapshot-save -no-boot-anim -gpu host \
    > "$ROOT/build/logs/android-emulator.log" 2>&1 &
  "$ADB" wait-for-device
  for _ in $(seq 1 100); do
    [ "$("$ADB" shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')" = 1 ] && break; sleep 3
  done
  echo "emulator klaar: Android $("$ADB" shell getprop ro.build.version.release | tr -d '\r')"
  ;;
install)
  APK="$(ls "$ROOT/build/android/apk-${2:?naam}/app/build/outputs/apk/debug/"*.apk)"
  "$ADB" install -r "$APK"
  ;;
open)
  "$ADB" logcat -c
  "$ADB" shell am start -W -n "$PKG/org.qtproject.qt5.android.bindings.QtActivity"
  ;;
shot)
  F="${2:-$ROOT/build/android/shots/emu-$(date +%H%M%S).png}"
  mkdir -p "$(dirname "$F")"
  "$ADB" exec-out screencap -p > "$F" && echo "$F"
  ;;
log)
  "$ADB" shell pidof "$PKG" >/dev/null && echo "app draait" || echo "app draait niet"
  "$ADB" logcat -d | grep -E 'FATAL|E AndroidRuntime|F DEBUG|F libc|UnsatisfiedLink|dlopen failed|NoSuchMethod|JNI DETECTED|SIGSEGV|SIGABRT| OpenCPN ' | tail -40
  ;;
ydnr)
  : "${YDNR:?YDNR ontbreekt in $CONF}"
  # Alleen verbinden en lezen; stdin is leeg, er wordt niets verstuurd.
  "$ADB" shell "cd /data/local/tmp; for p in 1457 10110; do \
     timeout 8 toybox nc -w 5 $YDNR \$p </dev/null > ydnr.\$p 2>&1; \
     echo \"$YDNR:\$p exit \$? bytes \$(wc -c < ydnr.\$p)\"; head -2 ydnr.\$p; rm -f ydnr.\$p; done"
  ;;
stop)
  "$ADB" emu kill
  ;;
*) sed -n 2,14p "$0"; exit 2 ;;
esac
