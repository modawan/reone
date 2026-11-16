#!/usr/bin/env bash
# Build wxWidgets for the Linux release packages and install it into PREFIX.
#
# usage: build-wxwidgets.sh PREFIX
#
# vcpkg's wxWidgets port builds its own GTK 3 (with GLib, Cairo, Pango, ATK,
# X11, D-Bus, curl and OpenSSL) on Linux, and a GTK bundled into the
# AppImage would not match the desktop's themes, input methods or settings.
# So wxWidgets is built here from its release sources against the system's
# GTK 3, which the packages then take from the system they run on. The
# libraries wxWidgets can carry inside itself (PNG, JPEG, TIFF, WebP, zlib,
# Expat, PCRE2) are built in, so the packages do not depend on the system's
# versions of those, which differ between distributions (libjpeg.so.8 on
# Ubuntu, libjpeg.so.62 on Fedora and Debian). Features reone does not use and
# that would pull in more system libraries are off, and so is X11 session
# management (libSM, libICE), which minimal desktops leave out.
#
# OpenGL is linked as libGL.so.1, which every driver provides, rather than
# GLVND's libOpenGL.so.0, which Debian and Ubuntu package separately and do
# not install with Mesa; --as-needed drops GLU and anything else wxWidgets
# links without calling.
#
# The version is the one vcpkg 2026.10.16 provides, which the Windows build
# uses.
set -euo pipefail

prefix=$1
version=3.3.3
sha256=81b09d6dd9f1ed9301f8c55a968a488d0491f264dc2bab19a7e407ac67009482
url=https://github.com/wxWidgets/wxWidgets/releases/download/v$version/wxWidgets-$version.tar.bz2

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

curl -fsSL --retry 3 -o "$work/wx.tar.bz2" "$url"
echo "$sha256  $work/wx.tar.bz2" | sha256sum -c -
tar -C "$work" -xjf "$work/wx.tar.bz2"

cmake -S "$work/wxWidgets-$version" -B "$work/build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$prefix" \
    -DCMAKE_INSTALL_LIBDIR=lib \
    -DCMAKE_SHARED_LINKER_FLAGS="-Wl,--as-needed" \
    -DOpenGL_GL_PREFERENCE=LEGACY \
    -DwxBUILD_SHARED=ON \
    -DwxBUILD_TOOLKIT=gtk3 \
    -DwxBUILD_SAMPLES=OFF \
    -DwxBUILD_TESTS=OFF \
    -DwxBUILD_DEMOS=OFF \
    -DwxBUILD_BENCHMARKS=OFF \
    -DwxUSE_OPENGL=ON \
    -DwxUSE_STC=ON \
    -DwxUSE_AUI=ON \
    -DwxUSE_LIBPNG=builtin \
    -DwxUSE_LIBJPEG=builtin \
    -DwxUSE_LIBTIFF=builtin \
    -DwxUSE_LIBWEBP=builtin \
    -DwxUSE_ZLIB=builtin \
    -DwxUSE_EXPAT=builtin \
    -DwxUSE_REGEX=builtin \
    -DwxUSE_LIBLZMA=OFF \
    -DwxUSE_LIBMSPACK=OFF \
    -DwxUSE_LIBSDL=OFF \
    -DwxUSE_LIBNOTIFY=OFF \
    -DwxUSE_SECRETSTORE=OFF \
    -DwxUSE_SPELLCHECK=OFF \
    -DwxUSE_WEBVIEW=OFF \
    -DwxUSE_MEDIACTRL=OFF \
    -DwxUSE_WEBREQUEST=OFF \
    -DwxUSE_WEBREQUEST_CURL=OFF \
    -DwxUSE_XTEST=OFF \
    -DwxUSE_DETECT_SM=OFF
cmake --build "$work/build"
cmake --install "$work/build"

# The components reone asks for (core base adv stc gl aui): a feature that
# silently fell off, e.g. OpenGL when its headers are missing, otherwise only
# shows up as an opaque find_package failure in reone's configure.
status=0
for lib in baseu gtk3u_core gtk3u_adv gtk3u_stc gtk3u_gl gtk3u_aui; do
    if ! ls "$prefix"/lib/libwx_$lib-*.so >/dev/null 2>&1; then
        echo "wxWidgets was built without libwx_$lib"
        status=1
    fi
done
if [ "$status" != 0 ]; then
    ls -l "$prefix/lib"
    exit 1
fi

# And that reone's find_package of them succeeds: wxWidgetsConfig.cmake
# looks up its own dependencies, and one that is missing (CURL when
# wxUSE_WEBREQUEST_CURL is left on, say) fails reone's configure with
# nothing but "cmake_policy PUSH without matching POP".
mkdir -p "$work/find"
cat >"$work/find/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.16)
project(find_wx CXX)
find_package(wxWidgets CONFIG REQUIRED COMPONENTS core base adv stc gl aui)
message(STATUS "wxWidgets ${wxWidgets_VERSION}: ${wxWidgets_LIBRARIES}")
CMAKE
cmake -S "$work/find" -B "$work/find/build" -DCMAKE_PREFIX_PATH="$prefix"
