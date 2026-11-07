#!/usr/bin/env bash
# Tests for nix/reone-run.sh against a stub store. No C++ build needed.
# Usage: reone-run-test.sh path/to/reone-run.sh
set -euo pipefail

script=${1:?usage: reone-run-test.sh path/to/reone-run.sh}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# A store with the same layout the package installs.
store=$work/store
mkdir -p "$store/bin" "$store/libexec/reone" "$store/share/reone"
for app in engine launcher toolkit; do
  printf '#!/bin/sh\necho "ran %s in $PWD with $*"\n' "$app" > "$store/libexec/reone/$app"
  chmod +x "$store/libexec/reone/$app"
done
: > "$store/share/reone/shaderpack.erf"
: > "$store/share/reone/cmudict.dict"
# Same substitution the package does. The Nix sandbox has no /usr/bin/env, so
# point the shebang at this bash, as the package's patchShebangs would.
sed -e "s|@out@|$store|" -e "1s|.*|#!$BASH|" "$script" > "$store/libexec/reone/reone-run"
chmod +x "$store/libexec/reone/reone-run"
for name in reone reone-launcher reone-toolkit; do
  ln -s ../libexec/reone/reone-run "$store/bin/$name"
done

fails=0
check() { # name, expected substring, actual
  if [[ "$3" == *"$2"* ]]; then
    echo "ok   $1"
  else
    echo "FAIL $1: expected [$2] in:"
    printf '%s\n' "$3" | sed 's/^/     /'
    fails=$((fails + 1))
  fi
}
fresh() { rm -rf "$work/home" "$work/caller"; mkdir -p "$work/caller"; }
# A failed run is reported by the checks that follow, not by set -e.
run() { (cd "$work/caller" && REONE_HOME=$work/home REONE_RUN_PRINT=1 "$@" 2>&1) || echo "exit=$?"; }

fresh
out=$(run "$store/bin/reone" --game k1 --width 800)
check "relative --game is resolved against the caller" "arg=$work/caller/k1" "$out"
check "other args pass through in order" $'arg=--width\narg=800' "$out"
check "runs in REONE_HOME" "cwd=$work/home" "$out"
check "bin/reone picks the engine" "exec=$store/libexec/reone/engine" "$out"

fresh
out=$(run "$store/bin/reone" --game=../games/k1)
check "--game=relative is resolved" "arg=--game=$work/caller/../games/k1" "$out"

fresh
out=$(run "$store/bin/reone" --game /abs/k1)
check "absolute --game is kept" "arg=/abs/k1" "$out"

fresh
out=$(run "$store/bin/reone")
check "no --game defaults to the caller's directory" $'arg=--game\narg='"$work/caller" "$out"

fresh
mkdir -p "$work/home"
echo "game=/from/launcher" > "$work/home/reone.cfg"
out=$(run "$store/bin/reone")
[[ "$out" != *"arg=--game"* ]] && echo "ok   reone.cfg game= is not overridden" \
  || { echo "FAIL reone.cfg game= was overridden: $out"; fails=$((fails + 1)); }

fresh
out=$(run "$store/bin/reone-launcher" --foo)
check "bin/reone-launcher picks the launcher" "exec=$store/libexec/reone/launcher" "$out"
[[ "$out" != *"arg=--game"* ]] && echo "ok   launcher gets no injected --game" \
  || { echo "FAIL launcher got --game: $out"; fails=$((fails + 1)); }

fresh
out=$(run "$store/bin/reone-toolkit")
check "bin/reone-toolkit picks the toolkit" "exec=$store/libexec/reone/toolkit" "$out"
[ -L "$work/home/cmudict.dict" ] && echo "ok   cmudict.dict linked" \
  || { echo "FAIL cmudict.dict not linked"; fails=$((fails + 1)); }
for f in shaderpack.erf engine; do
  [ -L "$work/home/$f" ] && echo "ok   $f linked" || { echo "FAIL $f not linked"; fails=$((fails + 1)); }
done

fresh
mkdir -p "$work/home"
echo mine > "$work/home/shaderpack.erf"
out=$(run "$store/bin/reone")
[[ "$out" == *"is not a link"*"exit=1"* ]] && [ "$(cat "$work/home/shaderpack.erf")" = mine ] \
  && echo "ok   a real shaderpack.erf is not replaced" \
  || { echo "FAIL real file replaced or no error: $out"; fails=$((fails + 1)); }

fresh
out=$(run "$store/libexec/reone/reone-run" bogus)
check "unknown app is rejected" "exit=2" "$out"

fresh
out=$( (cd "$work/caller" && REONE_HOME=$work/home "$store/bin/reone" --game k1 2>&1) || echo "exit=$?")
check "real exec runs the app in REONE_HOME" "ran engine in $work/home with --game $work/caller/k1" "$out"

if [ "$fails" -ne 0 ]; then
  echo "$fails failure(s)"
  exit 1
fi
echo "all reone-run tests passed"
