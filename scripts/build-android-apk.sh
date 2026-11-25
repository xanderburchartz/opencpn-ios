#!/bin/bash
# Android stap B: maakt een debug-APK (arm64) van een kernbuild uit
# scripts/build-android-core.sh, met de Android-app van bdbcat/OpenCPN-Android (Gen1,
# GPL-2.0, Qt 5.12.2 zoals OCPNAndroidCoreBuildSupport).
#
# Gebruik:
#   scripts/build-android-apk.sh <naam>        # bijv. master -> build/android/master-arm64
# Uitvoer: build/android/apk-<naam>/app/build/outputs/apk/debug/*.apk
#
# Wat er in de bouwkopie verandert (bron: build/android/bron/OpenCPN-Android, niet in de fork):
# - Firebase Analytics en Crashlytics eruit (plugins, afhankelijkheden, de twee regels in
#   QtActivity.java), zodat de test-APK niets naar de Firebase van bdbcat stuurt; er is ook
#   geen google-services.json.
# - Paden naar /home/dsr/... worden de Qt uit de support-zip en onze kernbuild.
# - Alleen arm64-v8a; de o-charts-server (oexserverd) wordt niet gekopieerd.
# - src/bitmaps heet in OpenCPN master resources/bitmaps.
# Omgeving: GRADLE_USER_HOME (standaard build/android/gradle-home).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
NAAM="${1:-}"
case "$NAAM" in ''|*/*|.*) sed -n 2,19p "$0"; exit 2 ;; esac
OUT="$ROOT/build/android"
BRON="$OUT/bron/OpenCPN-Android"
APP="$OUT/apk-$NAAM"
SRC="$OUT/src-$NAAM"
CORE="$OUT/$NAAM-arm64"
QT="$OUT/cache/OCPNAndroidCoreBuildSupport/qt5"
[ -f "$CORE/libgorp.so" ] || { echo "eerst scripts/build-android-core.sh $NAAM" >&2; exit 1; }
[ -d "$BRON" ] || git clone -q --depth 1 https://github.com/bdbcat/OpenCPN-Android.git "$BRON"

export JAVA_HOME="$HOME/Library/Android/jdk-17/Contents/Home"
export ANDROID_HOME="$HOME/Library/Android/sdk"
export GRADLE_USER_HOME="${GRADLE_USER_HOME:-$OUT/gradle-home}"

rsync -a --delete --exclude=.git --exclude='/app/build/' --exclude='/.gradle/' "$BRON/" "$APP/"
echo "sdk.dir=$ANDROID_HOME" > "$APP/local.properties"

python3 -I - "$APP" "$QT" "$SRC" "$CORE" <<'EOF'
import re, sys, pathlib
app, qt, src, core = sys.argv[1:]
def edit(path, fn):
    p = pathlib.Path(app, path); s = p.read_text(); n = fn(s)
    if n == s: sys.exit(f"geen wijziging in {path}")
    p.write_text(n)

def top(s):
    return "\n".join(l for l in s.split("\n")
                     if "firebase.crashlytics" not in l and "gms.google-services" not in l)
edit("build.gradle", top)

def appg(s):
    s = "\n".join(l for l in s.split("\n")
                  if not re.search(r"firebase|crashlytics|gms\.google-services", l))
    s = re.sub(r'def Qt_Base = ".*"', f'def Qt_Base = "{qt}"', s)
    s = re.sub(r'def OCPN_Base = ".*"', f'def OCPN_Base = "{src}"', s)
    s = re.sub(r'def OCPN_Build_64 = ".*"', f'def OCPN_Build_64 = "{core}"', s)
    s = s.replace('"${OCPN_Base}/${OCPN_Build_64}', '"${OCPN_Build_64}')
    s = s.replace("${OCPN_Base}/src/bitmaps/", "${OCPN_Base}/resources/bitmaps/")
    s = s.replace("ndk.abiFilters 'armeabi-v7a','arm64-v8a'", "ndk.abiFilters 'arm64-v8a'")
    s = s.replace('abiFilters "armeabi-v7a"', '// abiFilters "armeabi-v7a"')
    s = re.sub(r"\n\s*preBuild\.dependsOn\(copySERVERFiles(32|64)\)", "", s)
    return s
edit("app/build.gradle", appg)

def act(s):
    s = s.replace("import com.google.firebase.analytics.FirebaseAnalytics;", "")
    s = s.replace("private FirebaseAnalytics mFirebaseAnalytics;", "")
    s = s.replace("mFirebaseAnalytics = FirebaseAnalytics.getInstance(this);", "")
    return s
edit("app/src/main/java/org/qtproject/qt5/android/bindings/QtActivity.java", act)
EOF
# Controle: geen Firebase meer buiten commentaarregels.
if grep -rniE 'firebase|crashlytics' "$APP/app/src/main/java" "$APP/app/build.gradle" "$APP/build.gradle" \
     | grep -vE ':[0-9]+:\s*//'; then
  echo "Firebase nog niet helemaal weg (zie boven)" >&2
  exit 1
fi

cd "$APP"
chmod +x gradlew
./gradlew --no-daemon :app:assembleDebug
ls -l app/build/outputs/apk/debug/*.apk
echo "== klaar: $(ls app/build/outputs/apk/debug/*.apk)"
