#!/bin/bash
# Maakt een schone kopie van deze repo voor openbare publicatie, zonder werknotities en
# persoonsgegevens. De privérepo blijft het werkarchief; de kopie wordt de eerste commit van
# een aparte openbare repo.
#
# Gebruik: scripts/export-openbaar.sh [doelmap]      (standaard build/openbaar)
#
# - Neemt de bestanden die git kent plus nieuwe, niet-genegeerde bestanden (werkboom, dus ook
#   wat nog niet gecommit is).
# - Laat werknotities weg (lijst UITSLUITEN hieronder).
# - Controleert de kopie op patronen uit prive/patronen.txt (staat niet in git; één
#   uitgebreide reguliere expressie per regel). Bij een treffer: exit 1 met de vindplaatsen.
# - Meldt markdownlinks naar bestanden die in de kopie ontbreken.
# Er wordt niets gecommit, gepusht of aangemaakt op GitHub.
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DOEL="${1:-$ROOT/build/openbaar}"
PATRONEN="$ROOT/prive/patronen.txt"

# Werknotities (logboek, plan, lessen, sessie-afspraken, afbeeldingen van het logboek) en de
# Nederlandse docs; de openbare repo krijgt alleen de Engelse (README.en.md, docs/building.md,
# docs/o-charts.md).
UITSLUITEN='^(CLAUDE\.md|PLAN\.md|lessen\.md|docs/logboek\.md|docs/overzicht-[^/]*\.md|docs/bronnen\.md|docs/concept-[^/]*|docs/zelf-bouwen\.md|docs/o-charts-ios\.md|docs/qt-installeren\.md|docs/licenties-verspreiding\.md|docs/img/.*|prive/.*)$'

[ -f "$PATRONEN" ] || { echo "ontbreekt: $PATRONEN (lijst met persoonsgegevens)" >&2; exit 2; }
case "$DOEL" in
  "$ROOT"/build/*) ;;
  *) echo "doelmap moet onder $ROOT/build/ liggen: $DOEL" >&2; exit 2 ;;
esac

if [ -d "$DOEL" ]; then
  # Alleen een eerdere export leegmaken (herkenbaar aan het merkbestand). De map .git blijft
  # staan: build/openbaar is de werkkopie van de openbare repo, zodat een nieuwe export een
  # gewone commit bovenop de vorige wordt.
  [ -f "$DOEL/.export-openbaar" ] || { echo "$DOEL bestaat en is geen eerdere export" >&2; exit 2; }
  find "$DOEL" -mindepth 1 -maxdepth 1 ! -name .git -exec rm -rf -- {} +
fi
mkdir -p "$DOEL"
touch "$DOEL/.export-openbaar"

cd "$ROOT"
n=0
while IFS= read -r f; do
  [ -f "$f" ] || continue                         # verwijderd in de werkboom
  if [[ "$f" =~ $UITSLUITEN ]]; then continue; fi
  mkdir -p "$DOEL/$(dirname "$f")"
  cp -p "$f" "$DOEL/$f"
  n=$((n + 1))
done < <( { git ls-files; git ls-files --others --exclude-standard; } | sort -u)
# Openbare README is de Engelse; de Nederlandse README is de werkversie en blijft privé.
if [ -f "$DOEL/README.en.md" ]; then
  mv -f "$DOEL/README.en.md" "$DOEL/README.md"
fi
echo "gekopieerd: $n bestanden naar $DOEL"

# Persoonsgegevens: ook in binaire bestanden zoeken (-a), behalve het merkbestand.
fout=0
treffers="$(mktemp)"
if grep -rnaE -f "$PATRONEN" --exclude=.export-openbaar "$DOEL" >"$treffers" 2>/dev/null; then
  echo "PERSOONSGEGEVENS GEVONDEN:" >&2
  cut -c1-200 "$treffers" | sed "s#^$DOEL/##" >&2
  fout=1
fi
rm -f "$treffers"

# Links in markdown naar bestanden die niet in de kopie staan.
while IFS= read -r md; do
  dir="$(dirname "$md")"
  { grep -oE '\]\([^)#:]+\)' "$md" || true; } | sed -E 's/^\]\(//; s/\)$//' | while IFS= read -r l; do
    [ -e "$dir/$l" ] || echo "kapotte link: ${md#$DOEL/} -> $l" >&2
  done
done < <(find "$DOEL" -name '*.md')

if [ "$fout" -ne 0 ]; then
  echo "export niet bruikbaar: haal bovenstaande gegevens weg en draai opnieuw" >&2
  exit 1
fi
echo "geen persoonsgegevens gevonden"
