#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-License-Identifier: BSD-3-Clause
#
# Everything that has to be true of a demo AppImage. An AppImage that was
# produced is not one that runs, and the ways it can be built wrong are quiet:
# a payload built against too new a glibc, a library that was meant to be
# bundled and was not, a copy of the host's GL stack that was.
#
# Usage: packaging/appimage/verify.sh <appimage>

set -euo pipefail

if [ "$#" -ne 1 ]; then
    echo "usage: $0 <appimage>" >&2
    exit 2
fi
appimage="$(readlink -f "$1")"
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
chmod +x "$appimage"
echo "== $(basename "$appimage") ($(du -h "$appimage" | cut -f1))"

# There is no FUSE in a container or on a stock runner, and this is the same
# fallback a user without FUSE gets, so it is worth exercising anyway.
export APPIMAGE_EXTRACT_AND_RUN=1

echo "-- it walks every feature itself"
QT_QPA_PLATFORM=offscreen "$appimage" --self-test | tail -1

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
(cd "$work" && "$appimage" --appimage-extract >/dev/null)
root="$work/squashfs-root"

echo "-- the RHEL 8 floor, for everything inside it"
mapfile -t files < <(find "$root" -type f \( -name '*.so*' -o -perm -u+x \))
bash "$here/check-floor.sh" 2.28 3.4.25 1.3.11 "${files[@]}"

# Bundling any of these would put the build machine's copy in front of the
# host's driver, X server, compositor or C library.
echo "-- what it must not carry"
for forbidden in libGL.so libGLX.so libEGL.so libGLdispatch.so libOpenGL.so \
                 libX11.so libxcb.so.1 libwayland-client.so libc.so.6 \
                 libstdc++.so libgcc_s.so; do
    found="$(find "$root" -name "$forbidden*" -print -quit)"
    if [ -n "$found" ]; then
        echo "::error::${found#"$root"/} is bundled; it must come from the host"
        exit 1
    fi
done
echo "the driver, X11, Wayland and C runtime stacks are the host's"

echo "-- its desktop entry and icon"
desktop="$(find "$root" -maxdepth 1 -name '*.desktop' | head -1)"
test -n "$desktop"
grep -q '^Icon=plotcore$' "$desktop"
test -f "$root/plotcore.png"
test -f "$root/.DirIcon"
echo "$(basename "$desktop") and its icon are there"

# plotcore's licence, and the licences of everything else in the image:
# notices.py reads the image back against the folder it wrote, so a library
# that ever comes to be bundled without its notice fails here.
echo "-- the licences of what it carries"
test -f "$root/usr/share/doc/plotcore/LICENSE"
"${PLOTCORE_PYTHON:-python3}" "$here/../notices.py" check "$root" \
    "$root/usr/share/doc/plotcore/licenses"
