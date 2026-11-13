#!/usr/bin/env bash
# Haalt de broncode van derden op in upstream/, elk op een vastgelegde commit.
# upstream/ staat buiten git (zie .gitignore) en mag weg; dit script haalt hem terug.
#
# Gebruik:
#   scripts/fetch-upstream.sh              # alles
#   scripts/fetch-upstream.sh opencpn wx   # alleen deze onderdelen
#   scripts/fetch-upstream.sh --lijst      # toon onderdelen en commits
#
# Onderdelen:
#   opencpn       OpenCPN, release-tag (bouwbron)
#   wx            wxWidgets, officiële release met submodules (bouwbron voor wxQt)
#   wx-bdbcat     fork van bdbcat, tak "OpenCPN" (alleen referentie, niet de Android-bron)
#   android-wxinc headers van de wxQt die de Android-build gebruikt, uit
#                 bdbcat/OCPNAndroidCommon (alleen wxWidgets/include, referentie)
#   curl          libcurl (iOS heeft geen libcurl; OpenCPN gebruikt het voor downloads)
#   openssl       OpenSSL 3.5 LTS, TLS voor libcurl (https); Apache-2.0
#   shapelib      shapelib v1.6.1, die OpenCPN anders zelf downloadt (libs/shapelib)
#   rapidjson     rapidjson v1.1.0, die OpenCPN anders zelf downloadt (libs/rapidjson)
#   navico_autopilot_pi  eigen Navico-autopilotplugin (aparte repo, standaard de map
#                 ../navico_autopilot_pi naast deze repo; ander pad met NAVICO_PI_REPO)
#   o-charts_pi   o-charts-plugin (GPL-2.0), daarna scripts/apply-patches.sh o-charts_pi;
#                 de meegeleverde oexserverd-binaries (andere platforms) worden niet gebruikt,
#                 zie docs/o-charts-ios.md
#
# Bestaat een map al op de juiste commit, dan doet het script niets. Staat hij op een
# andere commit en zijn er geen lokale wijzigingen, dan zet het script hem om. Met lokale
# wijzigingen stopt het: eigen wijzigingen horen in patches/, niet in upstream/.
#
# Licenties: OpenCPN GPL-2.0-or-later, wxWidgets wxWindows Library Licence.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
UP="${UPSTREAM_DIR:-$ROOT/upstream}"   # UPSTREAM_DIR alleen om het script elders te testen
NAVICO_PI_REPO="${NAVICO_PI_REPO:-$(cd "$ROOT/.." && pwd)/navico_autopilot_pi}"
# Geen lokale kloon: van GitHub (zelf bouwen door anderen, 13 nov 2026).
[ -d "$NAVICO_PI_REPO" ] || NAVICO_PI_REPO="https://github.com/xanderburchartz/navico_autopilot_pi.git"

# naam | map | repo-url | ref (tag of tak, ter info) | commit | submodules (ja/nee) | sparse-pad
COMPONENTS=(
  "opencpn|OpenCPN|https://github.com/OpenCPN/OpenCPN.git|Release_5.14.2|bd6986a0854d89c662a3d3cab011e629337fcb88|nee|"
  "wx|wxWidgets|https://github.com/wxWidgets/wxWidgets.git|v3.3.4|6a42a4b999b127cf28a8b5fe686da6b8ca93a9b8|ja|"
  "wx-bdbcat|wxWidgets-bdbcat|https://github.com/bdbcat/wxWidgets.git|OpenCPN|96400c0ce98007e6c015a618c64f35f42901b2d0|nee|"
  "android-wxinc|OCPNAndroidCommon|https://github.com/bdbcat/OCPNAndroidCommon.git|master|0bb3fe2ac79c25ed1a2d7943732203c2a4010035|nee|wxWidgets/include"
  "openssl|openssl|https://github.com/openssl/openssl.git|openssl-3.5.9|45e844fa2a14ec92d146bd8f5778ac130b6625fb|nee|"
  "curl|curl|https://github.com/curl/curl.git|curl-8_23_0|3163ec065643ba20709f190111f42c3e1fe95273|nee|"
  "shapelib|shapelib|https://github.com/OSGeo/shapelib.git|v1.6.1|8e23c958f3dd1f5cc37139ce4f4e2824a84bc56a|nee|"
  "rapidjson|rapidjson|https://github.com/Tencent/rapidjson.git|v1.1.0|f54b0e47a08782a6131cc3d60f94d038fa6e0a51|nee|"
  "radar_pi|radar_pi|https://github.com/opencpn-radar-pi/radar_pi.git|v5.7.2|b259bf08a024bc416e2de136d58dfeb34bd30be2|ja|"
  "watchdog_pi|watchdog_pi|https://github.com/rgleason/watchdog_pi.git|v2.5.4.0|96505b0c367d1920a1770c1461d29997765b31b9|ja|"
  "vdr_pi|vdr_pi|https://github.com/rgleason/vdr_pi.git|v1.4.00.0|313a1df7a0415806b1df9df67fd0f83671df5c71|ja|"
  "polar_pi|polar_pi|https://github.com/rgleason/polar_pi.git|v1.2.38.0|10e82348848c6fc91e9b46fa1c22f86cfd9cf174|ja|"
  "tactics_pi|tactics_pi|https://github.com/rgleason/tactics_pi.git|v1.0.99.0|21bf683469f197c0e5d5185e120bd3c49cf9336d|ja|"
  "weather_routing_pi|weather_routing_pi|https://github.com/quinton-hoole/weather_routing_pi.git|1.15.45.9 (catalogus)|64ca7af0655c32ed4005973cee9ae7993c220efd|ja|"
  "raymarine_autopilot_pi|raymarine_autopilot_pi|https://github.com/rgleason/raymarine_autopilot_pi.git|v2.3.5.0|477a6c6ebbd0242c72aeeb8e4e34a8bf472aec00|ja|"
  "pypilot_pi|pypilot_pi|https://github.com/rgleason/pypilot_pi.git|v0.53.4.0|8dcc83f26d8e090f09614b5463c0fdb28ac1f3e1|ja|"
  "autopilot_route_pi|autopilot_route_pi|https://github.com/rgleason/autopilot_route_pi.git|v0.4.42.0|d7b7791d3c26894c12a8ea079f473778f4726a7e|ja|"
  "navico_autopilot_pi|navico_autopilot_pi|$NAVICO_PI_REPO|main|cb762e57a33ed4dcee3186fc079b96703c1ae349|ja|"
  "o-charts_pi|o-charts_pi|https://github.com/bdbcat/o-charts_pi.git|master (2.2.5.0)|cc4d4e75dec478f6e6a068c9d8e10813d2641903|ja|"
)

log() { printf '[fetch-upstream] %s\n' "$*"; }
die() { printf '[fetch-upstream] FOUT: %s\n' "$*" >&2; exit 1; }

command -v git >/dev/null || die "git niet gevonden"

fetch_one() {
  local name="$1" dir="$2" url="$3" ref="$4" commit="$5" subs="$6" sparse="$7"
  [ -n "$dir" ] || die "lege mapnaam voor $name"
  local target="$UP/$dir"

  if [ -d "$target/.git" ]; then
    local have
    have="$(git -C "$target" rev-parse HEAD 2>/dev/null || echo onbekend)"
    if [ "$have" = "$commit" ]; then
      log "$name: al op $ref ($commit), niets te doen"
      if [ -n "$(git -C "$target" status --porcelain 2>/dev/null)" ]; then
        log "$name: LET OP, lokale wijzigingen in $target (horen in patches/)"
      fi
      return 0
    fi
    if [ -n "$(git -C "$target" status --porcelain --ignore-submodules=none 2>/dev/null)" ]; then
      die "$name: $target heeft lokale wijzigingen; zet ze in patches/ en ruim de map zelf op"
    fi
    log "$name: staat op $have, zet om naar $ref ($commit)"
  elif [ -e "$target" ]; then
    die "$name: $target bestaat maar is geen git-checkout; kijk zelf wat erin staat"
  else
    log "$name: nieuw ophalen, $url @ $ref ($commit)"
    mkdir -p "$target"
    git -C "$target" init -q
    git -C "$target" remote add origin "$url"
    if [ -n "$sparse" ]; then
      git -C "$target" config remote.origin.promisor true
      git -C "$target" config remote.origin.partialclonefilter blob:none
      git -C "$target" sparse-checkout set --no-cone "/$sparse/"
    fi
  fi

  # Ophalen op exacte commit, zonder geschiedenis. GitHub staat fetch op hash toe.
  if [ -n "$sparse" ]; then
    git -C "$target" fetch -q --depth 1 --filter=blob:none origin "$commit"
  else
    git -C "$target" fetch -q --depth 1 origin "$commit"
  fi
  git -C "$target" -c advice.detachedHead=false checkout -q --force "$commit"

  local got
  got="$(git -C "$target" rev-parse HEAD)"
  [ "$got" = "$commit" ] || die "$name: verwacht $commit, gekregen $got"

  if [ "$subs" = "ja" ]; then
    log "$name: submodules ophalen"
    git -C "$target" submodule sync -q --recursive
    git -C "$target" submodule update -q --init --recursive --depth 1
  fi
  log "$name: klaar, HEAD $got"
}

write_versions() {
  local out="$UP/VERSIONS.txt"
  {
    echo "# Gegenereerd door scripts/fetch-upstream.sh op $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "# naam  map  ref  commit"
    local line name dir url ref commit subs sparse
    for line in "${COMPONENTS[@]}"; do
      IFS='|' read -r name dir url ref commit subs sparse <<<"$line"
      if [ -d "$UP/$dir/.git" ]; then
        echo "$name  $dir  $ref  $(git -C "$UP/$dir" rev-parse HEAD)"
        if [ "$subs" = "ja" ]; then
          git -C "$UP/$dir" submodule status --recursive | sed 's/^/    submodule /'
        fi
      fi
    done
  } >"$out"
  log "versies vastgelegd in $out"
}

if [ "${1:-}" = "--lijst" ]; then
  for line in "${COMPONENTS[@]}"; do
    IFS='|' read -r name dir url ref commit subs sparse <<<"$line"
    printf '%-14s %-20s %-16s %s\n' "$name" "upstream/$dir" "$ref" "$commit"
  done
  exit 0
fi

wanted=("$@")
mkdir -p "$UP"
found_any=0
for line in "${COMPONENTS[@]}"; do
  IFS='|' read -r name dir url ref commit subs sparse <<<"$line"
  if [ "${#wanted[@]}" -gt 0 ]; then
    match=0
    for w in "${wanted[@]}"; do [ "$w" = "$name" ] && match=1; done
    [ "$match" = 1 ] || continue
  fi
  found_any=1
  fetch_one "$name" "$dir" "$url" "$ref" "$commit" "$subs" "$sparse"
done
[ "$found_any" = 1 ] || die "onbekend onderdeel: ${wanted[*]} (zie --lijst)"
write_versions
