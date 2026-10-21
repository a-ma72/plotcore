#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-License-Identifier: BSD-3-Clause
#
# All three demo AppImages, built inside manylinux_2_28 -- AlmaLinux 8 with
# gcc-toolset, the image the wheel is built in -- so that each starts on
# RHEL 8, then checked there. What this installs is what the build links
# against and what gets bundled, which is why it comes from AlmaLinux 8 and
# EPEL 8 rather than from whatever runner it is on.
#
# Usage (from the repository's root, as CI runs it):
#   docker run --rm -v "$PWD:/io" -w /io quay.io/pypa/manylinux_2_28_x86_64 \
#       bash packaging/appimage/in-container.sh <plotcore-wheel> <output-dir>

set -euo pipefail

if [ "$#" -ne 2 ]; then
    echo "usage: $0 <plotcore-wheel> <output-dir>" >&2
    exit 2
fi
wheel="$1"
outdir="$2"
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# EPEL for xcb-util-cursor: Qt 6.5 and newer link the xcb plugin against it,
# RHEL 8 has it nowhere else, and so it is the one library both Qt images
# exist to carry.
dnf install -y -q epel-release
dnf install -y -q --setopt=install_weak_deps=False \
    mesa-libGL-devel mesa-libEGL-devel libxkbcommon-devel libxkbcommon-x11 \
    fontconfig-devel freetype-devel dbus-libs glib2 \
    libxcb libxcb-devel xcb-util xcb-util-cursor xcb-util-image xcb-util-keysyms \
    xcb-util-renderutil xcb-util-wm \
    wayland-devel libX11-devel libXcursor-devel libXrandr-devel libXi-devel

# Qt 6.8, the last whose Linux binaries need no more than glibc 2.28 (see
# qt-demo.sh), through the same tool install-qt-action uses on the runners.
python=/opt/python/cp312-cp312/bin/python
"$python" -m pip install -q aqtinstall==3.3.0
"$python" -m aqt install-qt linux desktop 6.8.3 linux_gcc_64 -O /opt/qt >/dev/null

# The toolchain rust/rust-toolchain.toml pins, installed by a pinned rustup.
curl -sSL --fail -o /tmp/rustup-init \
    https://static.rust-lang.org/rustup/archive/1.28.2/x86_64-unknown-linux-gnu/rustup-init
echo "20a06e644b0d9bd2fbdbfd52d42540bdde820ea7df86e92e533c073da0cdd43c  /tmp/rustup-init" \
    | sha256sum --check --status
chmod +x /tmp/rustup-init
/tmp/rustup-init -y -q --no-modify-path --default-toolchain none --profile minimal
export PATH="$HOME/.cargo/bin:$PATH"
(cd rust && rustup toolchain install)

bash "$here/qt-demo.sh" /opt/qt/6.8.3/gcc_64 "$outdir"
bash "$here/egui-demo.sh" "$outdir"
bash "$here/pyqt6-demo.sh" "$wheel" "$outdir"

for image in "$outdir"/*.AppImage; do
    bash "$here/verify.sh" "$image"
done

# Written as root into a directory the runner's user has to upload from.
chmod -R a+rwX "$outdir"
