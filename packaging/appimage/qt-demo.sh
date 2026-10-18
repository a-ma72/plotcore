#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-License-Identifier: GPL-3.0-only
#
# The Qt Widgets example as an AppImage.
#
# A prebuilt Qt is shared libraries and plugins, so unlike H5Scope's static
# executable there is a deployment problem here, and linuxdeploy's Qt plugin
# is what solves it: it asks qmake where the plugins are, copies the ones a
# Widgets program loads, and writes the qt.conf that finds them. The platform
# plugins are named rather than left to its default (xcb only): Wayland,
# because a desktop may have no X server at all, and offscreen, because that
# is what the self-test runs on.
#
# Qt 6.8 rather than the newest, because it is the last whose Linux binaries
# were built against glibc 2.28: 6.11's libQt6Core asks for GLIBC_2.34, which
# is RHEL 9.
#
# Usage: packaging/appimage/qt-demo.sh <qt-prefix> <output-dir>
#    eg: packaging/appimage/qt-demo.sh /opt/qt/6.8.3/gcc_64 dist

source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

if [ "$#" -ne 2 ]; then
    echo "usage: $0 <qt-prefix> <output-dir>" >&2
    exit 2
fi
qt="$(cd "$1" && pwd)"
outdir="$2"
version="$(sed -n 's/^project(plotcore VERSION \([0-9.]*\).*/\1/p' "$appimage_root/CMakeLists.txt")"

echo "plotcore $version Qt demo -> AppImage"

build="$appimage_root/build/appimage-qt"
cmake -S "$appimage_root" -B "$build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$qt" \
    -DPLOTCORE_WITH_QT=ON \
    -DPLOTCORE_BUILD_TESTS=OFF \
    -DPLOTCORE_BUILD_CAPI=OFF \
    -DPLOTCORE_INSTALL=OFF
cmake --build "$build" --target plotcore-qt-demo --parallel
binary="$build/examples/qt/plotcore-qt-demo"

# linuxdeploy finds a plugin named linuxdeploy-plugin-<name>-<arch>.AppImage
# beside itself.
fetch "$LINUXDEPLOY_URL" "$LINUXDEPLOY_SHA256" "$appimage_tools/linuxdeploy-x86_64.AppImage"
fetch "$LINUXDEPLOY_QT_URL" "$LINUXDEPLOY_QT_SHA256" \
    "$appimage_tools/linuxdeploy-plugin-qt-x86_64.AppImage"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
appdir="$work/plotcore-qt-demo.AppDir"

# NO_STRIP: linuxdeploy's own strip is older than some of the libraries it is
# handed and fails on them, and there is nothing to gain -- a Release build
# carries no debug information, and the Qt libraries it declines to strip
# anyway.
NO_STRIP=1 \
QMAKE="$qt/bin/qmake" \
LD_LIBRARY_PATH="$qt/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
EXTRA_PLATFORM_PLUGINS="libqoffscreen.so;libqwayland-egl.so;libqwayland-generic.so" \
    "$appimage_tools/linuxdeploy-x86_64.AppImage" \
        --appdir "$appdir" \
        --executable "$binary" \
        --desktop-file "$appimage_packaging/appimage/plotcore-qt-demo.desktop" \
        --icon-file "$appimage_packaging/plotcore.png" \
        --plugin qt

unbundle_host_libraries "$appdir/usr/lib"
desktop_and_icon "$appdir" "$appimage_packaging/appimage/plotcore-qt-demo.desktop"
pack "$appdir" "$outdir/plotcore-qt-demo-$version-x86_64.AppImage"
