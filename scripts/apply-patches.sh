#!/usr/bin/env bash
# Past de eigen patches uit patches/<project>/*.patch toe op upstream/<project>.
# Al toegepaste patches worden overgeslagen. Volgorde: alfabetisch (0001-, 0002-, ...).
#
# Gebruik: scripts/apply-patches.sh [project ...]   (standaard: alle mappen in patches/)
#
# Een patch maken na een wijziging in upstream/<project>:
#   git -C upstream/<project> diff > patches/<project>/NNNN-korte-naam.patch
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
UP="${UPSTREAM_DIR:-$ROOT/upstream}"

if [ "$#" -gt 0 ]; then
  projects=("$@")
else
  projects=()
  for d in "$ROOT"/patches/*/; do
    [ -d "$d" ] && projects+=("$(basename "$d")")
  done
fi

for p in "${projects[@]}"; do
  target="$UP/$p"
  [ -d "$target/.git" ] || { echo "[apply-patches] FOUT: $target ontbreekt (draai scripts/fetch-upstream.sh)" >&2; exit 1; }
  for patch in "$ROOT"/patches/"$p"/*.patch; do
    [ -e "$patch" ] || continue
    name="$p/$(basename "$patch")"
    if git -C "$target" apply --reverse --check "$patch" 2>/dev/null; then
      echo "[apply-patches] $name: al toegepast"
    elif git -C "$target" apply --check "$patch" 2>/dev/null; then
      git -C "$target" apply "$patch"
      echo "[apply-patches] $name: toegepast"
    else
      echo "[apply-patches] FOUT: $name past niet op $target" >&2
      exit 1
    fi
  done
done
