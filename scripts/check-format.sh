#!/usr/bin/env bash
# Controleert de opmaak (clang-format 18, .clang-format van OpenCPN) van de bestanden die een
# reeks commits raakt, per commit, in dezelfde mappen als OpenCPN's CI (test-clang-format).
#
# Gebruik: scripts/check-format.sh <git-map> <basis> <tip>
#   bijv. scripts/check-format.sh upstream/OpenCPN-master 8c8d64570 ios-port-master
# Uitvoer per commit: de bestanden die clang-format anders zou opmaken. Exitcode 1 als er één is.
# clang-format: $CLANG_FORMAT, anders build/venv-clang-format/bin/clang-format.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
GITDIR="${1:?git-map}"; BASE="${2:?basis}"; TIP="${3:?tip}"
CF="${CLANG_FORMAT:-$ROOT/build/venv-clang-format/bin/clang-format}"
"$CF" --version | grep -q "version 18" || { echo "clang-format 18 nodig ($CF)" >&2; exit 2; }

inscope() {
  case "$1" in
    *.h|*.cpp) ;;
    *) return 1 ;;
  esac
  case "$1" in
    plugins/dashboard_pi/src/wxJSON/*|plugins/chartdldr_pi/src/unarr/*) return 1 ;;
    plugins/chartdldr_pi/src/tinyxml/*|plugins/grib_pi/src/bzip2/*) return 1 ;;
    model/*|gui/*|plugins/wmm_pi/src/*|plugins/dashboard_pi/src/*) return 0 ;;
    plugins/chartdldr_pi/src/*|plugins/grib_pi/src/*|plugins/demo_pi_sample/src/*) return 0 ;;
  esac
  return 1
}

tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
fout=0
for c in $(git -C "$GITDIR" rev-list --reverse "$BASE..$TIP"); do
  git -C "$GITDIR" show "$c:.clang-format" > "$tmp/.clang-format"
  slecht=()
  while IFS= read -r f; do
    inscope "$f" || continue
    git -C "$GITDIR" cat-file -e "$c:$f" 2>/dev/null || continue   # verwijderd
    mkdir -p "$tmp/$(dirname "$f")"
    git -C "$GITDIR" show "$c:$f" > "$tmp/$f"
    if ! (cd "$tmp" && "$CF" --style=file "$f" | cmp -s - "$f"); then slecht+=("$f"); fi
  done < <(git -C "$GITDIR" diff-tree --no-commit-id --name-only -r "$c")
  if [ ${#slecht[@]} -gt 0 ]; then
    fout=1
    echo "$(git -C "$GITDIR" log -1 --format='%h %s' "$c"): ${slecht[*]}"
  fi
done
[ $fout = 0 ] && echo "schoon: $(git -C "$GITDIR" rev-list --count "$BASE..$TIP") commits"
exit $fout
