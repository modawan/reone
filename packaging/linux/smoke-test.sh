#!/usr/bin/env bash
# Check that a reone AppImage runs on this system, as far as is possible
# without game data, which cannot be shipped or downloaded in CI.
#
# usage: smoke-test.sh APPIMAGE
#
# 1. Every bundled binary finds every library it needs on this system.
# 2. The AppImage itself starts (without FUSE) and reports its version.
# 3. The engine, started from an unrelated directory with no reone.cfg and a
#    stub game directory (a chitin.key that is not one, and the streamwaves
#    directory that marks KotOR), creates its (hidden, offscreen) window and
#    OpenGL context, finds and loads its shader pack, fails on the stub key
#    file, logs that cleanly to
#    ~/.config/reone/engine.log and writes nothing to the current directory.
#    Reaching the key file proves the shader pack was found: it is loaded
#    first, and a missing one fails the run before the key file is read.
#    The run ends before the first frame, so files written at exit after one
#    (imgui.ini) are not covered here.
#
# Run it on the oldest and newest distributions the AppImage claims to
# support, with only the system libraries it expects installed: an OpenGL
# driver (Mesa), GTK 3, and glibc's ldd.
set -euo pipefail

appimage=$(readlink -f "$1")
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cd "$tmp"

cp "$appimage" reone.AppImage
chmod +x reone.AppImage
./reone.AppImage --appimage-extract >/dev/null
root=$tmp/squashfs-root

echo "== libraries"
status=0
checked=0
while IFS= read -r -d '' f; do
    [ "$(head -c 4 "$f")" = $'\x7fELF' ] || continue
    checked=$((checked + 1))
    if missing=$(ldd "$f" | grep "not found"); then
        echo "${f#"$root"/}:"
        echo "$missing"
        status=1
    fi
done < <(find "$root/usr/bin" "$root/usr/libexec/reone" "$root/usr/lib/reone" -type f -print0)
[ "$checked" -gt 0 ] || { echo "no binaries found in the AppImage"; exit 1; }
[ "$status" = 0 ] || exit 1
echo "$checked binaries resolve all their libraries"

echo "== version"
version=$(./reone.AppImage --appimage-extract-and-run --version)
echo "$version"
case "$version" in
    "reone "?*) ;;
    *) echo "unexpected --version output"; exit 1 ;;
esac

echo "== engine"
export HOME=$tmp/home
unset XDG_CONFIG_HOME
export SDL_VIDEO_DRIVER=offscreen
# OpenAL's null backend: a container has no sound device, and minimal ones
# (Fedora's) not even ALSA, which every desktop has and OpenAL loads at run
# time. Without a device the engine stops before it gets to the shader pack.
export ALSOFT_DRIVERS=null
mkdir -p "$HOME" elsewhere stub-game/streamwaves
printf 'NOT A KEY FILE' >stub-game/chitin.key
status=0
(cd elsewhere && "$root/AppRun" --game "$tmp/stub-game" --headless 1) >engine.out 2>&1 || status=$?
cat engine.out
log=$HOME/.config/reone/engine.log
[ "$status" -ne 0 ] || { echo "the engine accepted a stub game directory"; exit 1; }
[ -f "$log" ] || { echo "no $log"; exit 1; }
cat "$log"
[ -z "$(ls -A elsewhere)" ] || { echo "the engine wrote to the current directory:"; ls -A elsewhere; exit 1; }
grep -q "Engine failure" "$log" || { echo "the engine did not log a clean failure"; exit 1; }
if grep -q "shaderpack.erf" "$log"; then
    echo "the engine did not find its shader pack"
    exit 1
fi
grep -q "KEY signature" "$log" || { echo "the engine failed before it read the game's key file"; exit 1; }
echo "smoke test passed"
