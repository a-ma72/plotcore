#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-License-Identifier: BSD-3-Clause
#
# Each AppImage's self-test on a stock RHEL 8 with an X server, which is the
# claim the AppImages make. Stock means BaseOS and AppStream only -- no EPEL,
# so no xcb-util-cursor, and none of the xcb-util family either: those are
# what the Qt images say they carry, and this is where that is found out. What
# is installed is what a RHEL 8 desktop has and the images leave to the host:
# the X and GL client libraries, fontconfig and a font, D-Bus, GLib.
#
# On X11 through Xvfb rather than offscreen, because the xcb plugin and what
# it links are exactly what offscreen never loads.
#
# Usage (as CI runs it):
#   docker run --rm -v "$PWD:/io" -w /io almalinux:8 \
#       bash packaging/appimage/try-on-rhel8.sh <output-dir>

set -euo pipefail

if [ "$#" -ne 1 ]; then
    echo "usage: $0 <output-dir>" >&2
    exit 2
fi
outdir="$1"

dnf install -y -q --setopt=install_weak_deps=False \
    xorg-x11-server-Xvfb mesa-libGL mesa-libEGL libX11 libX11-xcb libxcb \
    libwayland-client libwayland-cursor libwayland-egl \
    fontconfig dejavu-sans-fonts dbus-libs glib2 procps-ng

Xvfb :99 -screen 0 1600x1000x24 -nolisten tcp &
xvfb=$!
trap 'kill "$xvfb" 2>/dev/null || true' EXIT
for _ in $(seq 50); do
    [ -e /tmp/.X11-unix/X99 ] && break
    sleep 0.1
done
export DISPLAY=:99 QT_QPA_PLATFORM=xcb APPIMAGE_EXTRACT_AND_RUN=1

for image in "$outdir"/*.AppImage; do
    echo "== $(basename "$image") on $(. /etc/os-release && echo "$PRETTY_NAME"), X11"
    "$image" --self-test | tail -1
done
