#!/usr/bin/env bash
# Herschrijft een reeks commits zo dat elke commit de bestanden die de reeks tot dan toe raakt
# met clang-format 18 (stijl van OpenCPN) opgemaakt bevat. Inhoud, berichten, auteur en
# datums blijven gelijk; alleen de opmaak verandert. De basis moet zelf al schoon zijn.
#
# Gebruik: scripts/reformat-series.sh <git-map> <basis> <tip> <nieuwe-branch>
# Maakt <nieuwe-branch> aan (of zet hem opnieuw); de oude branch blijft staan.
# Alleen bestanden in de mappen die OpenCPN's CI controleert (zie check-format.sh).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
GITDIR="$(cd "${1:?git-map}" && pwd)"; BASE="${2:?basis}"; TIP="${3:?tip}"; NEW="${4:?branch}"
CF="${CLANG_FORMAT:-$ROOT/build/venv-clang-format/bin/clang-format}"
"$CF" --version | grep -q "version 18" || { echo "clang-format 18 nodig" >&2; exit 2; }
NAME="${OCPNIOS_GIT_NAME:-Xander Burchartz}"
EMAIL="${OCPNIOS_GIT_EMAIL:-210020462+xanderburchartz@users.noreply.github.com}"

inscope() {
  case "$1" in *.h|*.cpp) ;; *) return 1 ;; esac
  case "$1" in
    plugins/dashboard_pi/src/wxJSON/*|plugins/chartdldr_pi/src/unarr/*) return 1 ;;
    plugins/chartdldr_pi/src/tinyxml/*|plugins/grib_pi/src/bzip2/*) return 1 ;;
    model/*|gui/*|plugins/wmm_pi/src/*|plugins/dashboard_pi/src/*) return 0 ;;
    plugins/chartdldr_pi/src/*|plugins/grib_pi/src/*|plugins/demo_pi_sample/src/*) return 0 ;;
  esac
  return 1
}

WT="$ROOT/build/fmt-worktree-$$"
git -C "$GITDIR" worktree add -q --detach "$WT" "$BASE"
trap 'git -C "$GITDIR" worktree remove --force "$WT"' EXIT

prev="$(git -C "$GITDIR" rev-parse "$BASE")"
for c in $(git -C "$GITDIR" rev-list --reverse "$BASE..$TIP"); do
  git -C "$WT" reset -q --hard "$c"
  n=0
  while IFS= read -r f; do
    inscope "$f" || continue
    [ -f "$WT/$f" ] || continue
    (cd "$WT" && "$CF" -i --style=file "$f"); n=$((n+1))
  done < <(git -C "$GITDIR" diff --name-only "$BASE" "$c")
  git -C "$WT" add -A
  tree="$(git -C "$WT" write-tree)"
  new="$(GIT_AUTHOR_NAME="$(git -C "$GITDIR" log -1 --format=%an "$c")" \
    GIT_AUTHOR_EMAIL="$(git -C "$GITDIR" log -1 --format=%ae "$c")" \
    GIT_AUTHOR_DATE="$(git -C "$GITDIR" log -1 --format=%aD "$c")" \
    GIT_COMMITTER_NAME="$NAME" GIT_COMMITTER_EMAIL="$EMAIL" \
    GIT_COMMITTER_DATE="$(git -C "$GITDIR" log -1 --format=%cD "$c")" \
    git -C "$GITDIR" commit-tree "$tree" -p "$prev" < <(git -C "$GITDIR" log -1 --format=%B "$c"))"
  echo "$(git -C "$GITDIR" rev-parse --short "$c") -> $(git -C "$GITDIR" rev-parse --short "$new")  ($n bestanden opgemaakt)  $(git -C "$GITDIR" log -1 --format=%s "$c")"
  prev="$new"
done
git -C "$GITDIR" branch -f "$NEW" "$prev"
echo "$NEW = $(git -C "$GITDIR" rev-parse --short "$NEW")"
