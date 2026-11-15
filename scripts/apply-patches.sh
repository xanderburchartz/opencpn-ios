#!/usr/bin/env bash
# Past de eigen patches uit patches/<project>/*.patch toe op upstream/<project>.
# Al toegepaste patches worden overgeslagen. Volgorde: alfabetisch (0001-, 0002-, ...).
#
# Gebruik: scripts/apply-patches.sh [project ...]   (standaard: alle mappen in patches/,
#          behalve OpenCPN-fixes en OpenCPN-master)
#
# patches/OpenCPN-fixes/ (losse fix-PR's voor OpenCPN-master) en patches/OpenCPN-master/ (de
# reeks op master) horen niet bij de bouw van de app en hebben geen eigen map die
# fetch-upstream.sh ophaalt. Zonder argumenten worden ze overgeslagen; noem ze expliciet om ze
# toe te passen op upstream/OpenCPN-fixes of upstream/OpenCPN-master.
#
# Een patch maken na een wijziging in upstream/<project>:
#   git -C upstream/<project> diff > patches/<project>/NNNN-korte-naam.patch
# OpenCPN is een reeks commits (git format-patch); nieuwe wijziging = nieuwe commit:
#   git -C upstream/OpenCPN commit ...; rm patches/OpenCPN/*.patch
#   git -C upstream/OpenCPN format-patch --no-signature -o ../../patches/OpenCPN Release_5.14.2
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
UP="${UPSTREAM_DIR:-$ROOT/upstream}"

if [ "$#" -gt 0 ]; then
  projects=("$@")
else
  projects=()
  for d in "$ROOT"/patches/*/; do
    [ -d "$d" ] || continue
    p="$(basename "$d")"
    case "$p" in
      OpenCPN-fixes|OpenCPN-master) echo "[apply-patches] $p: overgeslagen (alleen expliciet)"; continue ;;
    esac
    projects+=("$p")
  done
fi

for p in "${projects[@]}"; do
  target="$UP/$p"
  [ -d "$target/.git" ] || { echo "[apply-patches] FOUT: $target ontbreekt (draai scripts/fetch-upstream.sh)" >&2; exit 1; }
  # Een reeks uit git format-patch (begint met "From <sha>") wordt met git am als commits
  # toegepast, zodat upstream/<project> de commitgeschiedenis heeft (zie docs/upstream.md).
  # Toegepast = het onderwerp van de laatste patch staat al in de geschiedenis.
  series=("$ROOT"/patches/"$p"/*.patch)
  if [ -e "${series[0]}" ] && head -1 "${series[0]}" | grep -q '^From [0-9a-f]\{40\} '; then
    last_subject="$(sed -n 's/^Subject: \[PATCH[^]]*\] //p' "${series[${#series[@]}-1]}" | head -1)"
    subjects="$(git -C "$target" log --format=%s)"
    if grep -qxF "$last_subject" <<<"$subjects"; then
      echo "[apply-patches] $p: reeks van ${#series[@]} commits al toegepast"
    else
      # Committer = het GitHub-noreply-adres, zoals in de fork (geen privé-adres in commits).
      # De auteur komt uit de patch.
      git -C "$target" -c user.name="${OCPNIOS_GIT_NAME:-Xander Burchartz}" \
        -c user.email="${OCPNIOS_GIT_EMAIL:-210020462+xanderburchartz@users.noreply.github.com}" \
        am --quiet --committer-date-is-author-date "${series[@]}" || {
        echo "[apply-patches] FOUT: reeks $p past niet op $target (git am --abort om terug te gaan)" >&2
        exit 1; }
      echo "[apply-patches] $p: reeks van ${#series[@]} commits toegepast"
    fi
    continue
  fi
  for patch in "${series[@]}"; do
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
