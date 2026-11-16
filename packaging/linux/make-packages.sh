#!/usr/bin/env bash
# Package a reone build as an AppImage and a tarball for x86_64 Linux.
#
# usage: make-packages.sh BUILD_DIR VERSION LIB_DIRS OUT_DIR
#
#   BUILD_DIR  a CMake build of reone configured with CMAKE_INSTALL_PREFIX=/usr
#   VERSION    goes into the file names, e.g. 0.23.0 or 0.23.0-rc1-12-gabc1234
#   LIB_DIRS   the directories, separated by colons, holding the shared
#              libraries the build linked against that are to be bundled
#              (vcpkg's installed/<triplet>/lib and wxWidgets' lib)
#   OUT_DIR    receives reone-VERSION-x86_64.AppImage,
#              reone-VERSION-linux-x86_64.tar.gz and
#              reone-VERSION-linux-x86_64.sha256
#
# What is bundled, deterministically: every shared library the applications
# load that resolves into LIB_DIRS, i.e. everything the build compiled itself.
# Everything else comes from the system it runs on: glibc, libstdc++, the
# OpenGL driver, X11 and Wayland (which SDL loads at run time), ALSA (which
# OpenAL Soft loads at run time) and GTK for the launcher and the toolkit.
# Bundling any of those breaks on systems whose drivers or plugins expect
# their own copy. The script fails if a bundled binary would still load
# anything from LIB_DIRS, and prints what it expects from the system.
#
# Needs: cmake, patchelf, strip, objdump, ldd, curl, sha256sum, tar. Downloads
# appimagetool and the AppImage runtime, both pinned by checksum.
set -euo pipefail

if [ $# -ne 4 ]; then
    sed -n '2,12p' "$0" >&2
    exit 2
fi
build_dir=$(readlink -f "$1")
version=$2
lib_dirs=()
IFS=: read -r -a given_lib_dirs <<<"$3"
for d in "${given_lib_dirs[@]}"; do
    lib_dirs+=("$(readlink -f "$d")")
done
lib_path=$(IFS=:; echo "${lib_dirs[*]}")
out_dir=$(readlink -m "$4")
here=$(dirname "$(readlink -f "$0")")

appimagetool_url=https://github.com/AppImage/appimagetool/releases/download/1.9.1/appimagetool-x86_64.AppImage
appimagetool_sha256=ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0
runtime_url=https://github.com/AppImage/type2-runtime/releases/download/20251108/runtime-x86_64
runtime_sha256=2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
appdir=$work/AppDir

# Whether a path lies in one of LIB_DIRS.
in_lib_dirs() {
    local d
    for d in "${lib_dirs[@]}"; do
        case "$1" in "$d"/*) return 0 ;; esac
    done
    return 1
}

is_elf() {
    [ -f "$1" ] && [ ! -L "$1" ] && [ "$(head -c 4 "$1")" = $'\x7fELF' ]
}

echo "== install"
DESTDIR=$appdir cmake --install "$build_dir" --prefix /usr

echo "== bundle libraries from $lib_path"
mkdir -p "$appdir/usr/lib/reone"
apps=()
for f in "$appdir"/usr/bin/* "$appdir"/usr/libexec/reone/*; do
    if is_elf "$f"; then
        apps+=("$f")
    fi
done
# ldd lists the whole dependency closure, so one pass over the applications
# finds every library to bundle. Each is copied under the name it is linked
# by (its soname), with symlinks resolved.
for f in "${apps[@]}"; do
    LD_LIBRARY_PATH=$lib_path ldd "$f" | awk '$2 == "=>" && $3 ~ /^\// { print $1, $3 }'
done | sort -u | while read -r soname path; do
    if in_lib_dirs "$(readlink -f "$path")"; then
        cp -L "$path" "$appdir/usr/lib/reone/$soname"
    fi
done
chmod 755 "$appdir"/usr/lib/reone/*

echo "== strip and set RUNPATH"
for f in "${apps[@]}" "$appdir"/usr/lib/reone/*; do
    strip --strip-unneeded "$f"
done
for f in "${apps[@]}"; do
    case "$f" in
        "$appdir"/usr/libexec/reone/*) patchelf --set-rpath '$ORIGIN/../../lib/reone' "$f" ;;
        *) patchelf --set-rpath '$ORIGIN/../lib/reone' "$f" ;;
    esac
done
for f in "$appdir"/usr/lib/reone/*; do
    patchelf --set-rpath '$ORIGIN' "$f"
done

echo "== check"
status=0
for f in "${apps[@]}" "$appdir"/usr/lib/reone/*; do
    deps=$(env -u LD_LIBRARY_PATH ldd "$f")
    if grep -q "not found" <<<"$deps"; then
        echo "${f#"$appdir"/}: missing libraries:"
        grep "not found" <<<"$deps"
        status=1
    fi
    while read -r soname path; do
        if in_lib_dirs "$(readlink -f "$path")"; then
            echo "${f#"$appdir"/}: still loads $soname from $path"
            status=1
        fi
    done < <(awk '$2 == "=>" && $3 ~ /^\// { print $1, $3 }' <<<"$deps")
done
# What a packaged binary links directly must be bundled or be something
# every desktop has: glibc, libstdc++, the OpenGL driver's libGL and libEGL,
# X11, Wayland and the GTK 3 stack. Anything else (libOpenGL.so.0 and
# libGLU.so.1, which Debian and Ubuntu do not install with Mesa; libSM and
# libICE) would fail to start on some of them.
system_libs='^(libc|libm|libdl|libpthread|librt|libutil|ld-linux-x86-64|libstdc\+\+|libgcc_s)\.so'
system_libs+='|^lib(GL|EGL|GLX)\.so|^libX[A-Za-z0-9]*\.so|^libxcb|^libxkbcommon|^libwayland-'
system_libs+='|^lib(gtk|gdk)-3\.so|^lib(glib|gobject|gio|gmodule|gthread)-2\.0\.so|^libgdk_pixbuf-2\.0\.so'
system_libs+='|^libpango|^libcairo|^libatk|^libharfbuzz|^libfontconfig|^libfreetype|^libz\.so'
for f in "${apps[@]}" "$appdir"/usr/lib/reone/*; do
    for needed in $(objdump -p "$f" | awk '$1 == "NEEDED" { print $2 }'); do
        [ -e "$appdir/usr/lib/reone/$needed" ] && continue
        if ! grep -qE "$system_libs" <<<"$needed"; then
            echo "${f#"$appdir"/} needs $needed, which is neither bundled nor on every desktop"
            status=1
        fi
    done
done
[ "$status" = 0 ] || exit 1
echo "bundled: $(cd "$appdir/usr/lib/reone" && ls | tr '\n' ' ')"
echo "from the system:"
for f in "${apps[@]}" "$appdir"/usr/lib/reone/*; do
    env -u LD_LIBRARY_PATH ldd "$f" | awk -v appdir="$appdir/" '$2 == "=>" && index($3, appdir) != 1 { print "  " $1 }'
done | sort -u

echo "== AppDir"
install -m755 "$here/AppRun" "$appdir/AppRun"
cp "$appdir/usr/share/applications/io.github.modawan.reone.desktop" "$appdir/"
cp "$appdir/usr/share/icons/hicolor/256x256/apps/io.github.modawan.reone.png" "$appdir/"
ln -s io.github.modawan.reone.png "$appdir/.DirIcon"
"$appdir/AppRun" --version

echo "== tools"
fetch() {
    curl -fsSL --retry 3 -o "$work/$2" "$1"
    echo "$3  $work/$2" | sha256sum -c -
}
fetch "$appimagetool_url" appimagetool "$appimagetool_sha256"
fetch "$runtime_url" runtime "$runtime_sha256"
chmod +x "$work/appimagetool"

mkdir -p "$out_dir"
appimage=reone-$version-x86_64.AppImage
tarball=reone-$version-linux-x86_64.tar.gz

echo "== $appimage"
# --appimage-extract-and-run: CI runners and containers have no FUSE.
ARCH=x86_64 VERSION=$version "$work/appimagetool" --appimage-extract-and-run \
    --no-appstream --runtime-file "$work/runtime" "$appdir" "$out_dir/$appimage"

echo "== $tarball"
top=reone-$version-linux-x86_64
cp -a "$appdir" "$work/$top"
ln -s AppRun "$work/$top/reone"
tar -C "$work" --sort=name --owner=0 --group=0 --numeric-owner \
    --mtime="@${SOURCE_DATE_EPOCH:-0}" -czf "$out_dir/$tarball" "$top"

(cd "$out_dir" && sha256sum "$appimage" "$tarball" >"reone-$version-linux-x86_64.sha256")
cat "$out_dir/reone-$version-linux-x86_64.sha256"
