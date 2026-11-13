#!/usr/bin/env bash
# Laat per onderdeel zien of er een nieuwere versie is dan wat vastligt in
# scripts/fetch-upstream.sh. Verandert niets: alleen lezen (git ls-remote en de
# plugincatalogus van OpenCPN).
#
# Gebruik:
#   scripts/check-updates.sh            # alle onderdelen
#   scripts/check-updates.sh radar_pi   # alleen deze
#
# Kolommen:
#   vast       ref en commit uit fetch-upstream.sh
#   nieuwste   hoogste versietag in de repo (git ls-remote, gesorteerd op versie)
#   catalogus  voor plugins: de versie in de officiële catalogus (OpenCPN/plugins,
#              ocpn-plugins.xml), de versie die OpenCPN op Mac/Linux aanbiedt
# Een tag-volgorde is niet altijd betrouwbaar (wisselende nummering); de catalogus is voor
# plugins leidend. Bijwerken: zie README.md, "Bijwerken".
#
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FETCH="$ROOT/scripts/fetch-upstream.sh"
CATALOG_URL="https://raw.githubusercontent.com/OpenCPN/plugins/master/ocpn-plugins.xml"

# Plugin-map -> naam in de catalogus (GetCommonName).
catalog_name() {
  case "$1" in
    radar_pi) echo "Radar" ;;
    watchdog_pi) echo "Watchdog" ;;
    vdr_pi) echo "VDR" ;;
    polar_pi) echo "Polar" ;;
    tactics_pi) echo "Tactics" ;;
    weather_routing_pi) echo "WeatherRouting" ;;
    raymarine_autopilot_pi) echo "Raymarine autopilot" ;;
    pypilot_pi) echo "pypilot" ;;
    autopilot_route_pi) echo "Autopilot Route" ;;
    *) echo "" ;;
  esac
}

# Tagvorm per onderdeel (regex); standaard v1.2.3 of 1.2.3.
tag_pattern() {
  case "$1" in
    opencpn) echo '^Release_[0-9]+(\.[0-9]+)+$' ;;
    curl) echo '^curl-[0-9]+(_[0-9]+)+$' ;;
    *) echo '^v?[0-9]+(\.[0-9]+)+' ;;
  esac
}

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
CATALOG="$TMP/ocpn-plugins.xml"
if ! curl -sSf -m 60 -o "$CATALOG" "$CATALOG_URL"; then
  echo "[check-updates] catalogus niet op te halen; alleen git-tags" >&2
  : > "$CATALOG"
fi

# Hoogste versie van een plugin in de catalogus (alle doelplatforms samen).
catalog_version() {
  local name="$1"
  [ -s "$CATALOG" ] || { echo "-"; return; }
  python3 -I - "$CATALOG" "$name" <<'PY'
import re, sys
xml = open(sys.argv[1], encoding="utf-8", errors="replace").read()
want = sys.argv[2]
vers = []
for b in re.findall(r"<plugin[^>]*>.*?</plugin>", xml, re.S):
    n = re.search(r"<name>\s*(.*?)\s*</name>", b)
    v = re.search(r"<version>\s*(.*?)\s*</version>", b)
    if n and v and n.group(1) == want:
        vers.append(v.group(1))
def key(s):
    return [int(x) if x.isdigit() else 0 for x in re.split(r"[.+-]", s.split("+")[0])]
print(max(vers, key=key) if vers else "-")
PY
}

# Onderdelen uit fetch-upstream.sh: naam|map|url|ref|commit|submodules|sparse
# (bash 3.2 op macOS: geen mapfile)
LINES_FILE="$TMP/components"
sed -n '/^COMPONENTS=(/,/^)/p' "$FETCH" | grep '^  "' | tr -d '"' | sed 's/^ *//' > "$LINES_FILE"

printf '%-22s %-26s %-22s %s\n' "onderdeel" "vast" "nieuwste tag" "catalogus"
while IFS= read -r line; do
  IFS='|' read -r name dir url ref commit subs sparse <<<"$line"
  if [ $# -gt 0 ]; then
    match=0
    for want in "$@"; do [ "$want" = "$name" ] && match=1; done
    [ $match = 1 ] || continue
  fi
  latest=$(GIT_TERMINAL_PROMPT=0 git -c http.lowSpeedLimit=1000 -c http.lowSpeedTime=30 ls-remote --tags --refs "$url" 2>/dev/null \
    | awk '{print $2}' | sed 's|refs/tags/||' \
    | grep -E "$(tag_pattern "$name")" | grep -viE 'beta|alpha|rc|ov[0-9]' \
    | sed 's/^curl-//; /^[0-9_]*$/s/_/./g' | sort -V | tail -1 || true)
  [ -n "$latest" ] || latest="-"
  cname=$(catalog_name "$name")
  if [ -n "$cname" ]; then cat_v=$(catalog_version "$cname"); else cat_v=""; fi
  printf '%-22s %-26s %-22s %s\n' "$name" "${ref} (${commit:0:7})" "$latest" "$cat_v"
done < "$LINES_FILE"
