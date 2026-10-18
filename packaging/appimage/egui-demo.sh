#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-License-Identifier: GPL-3.0-only
#
# The egui demo as an AppImage.
#
# The smallest of the three. Rust links its own code statically and plotcore's
# C++ through plotcore-sys, so the executable asks the host for glibc and the
# C++ runtime and nothing else at link time; the windowing and GL libraries --
# libX11, libxkbcommon, libwayland-client, libGL -- are opened at run time by
# winit and glow, and are the host's for the reasons common.sh gives. The walk
# below is run anyway, so a dependency that ever does appear is bundled rather
# than missed.
#
# Usage: packaging/appimage/egui-demo.sh <output-dir>

source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

if [ "$#" -ne 1 ]; then
    echo "usage: $0 <output-dir>" >&2
    exit 2
fi
outdir="$1"
version="$(sed -n 's/^version = "\([0-9.]*\)"/\1/p' "$appimage_root/rust/Cargo.toml" | head -1)"

echo "plotcore $version egui demo -> AppImage"

(cd "$appimage_root/rust" && cargo build --release --locked -p egui-demo)
binary="$appimage_root/rust/target/release/egui-demo"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
appdir="$work/plotcore-egui-demo.AppDir"

install -Dm 755 "$binary" "$appdir/usr/bin/plotcore-egui-demo"
cat > "$appdir/AppRun" <<'EOF'
#!/bin/sh
# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-License-Identifier: GPL-3.0-only
APPDIR="$(dirname "$(readlink -f "$0")")"
export LD_LIBRARY_PATH="$APPDIR/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$APPDIR/usr/bin/plotcore-egui-demo" "$@"
EOF
chmod 755 "$appdir/AppRun"

bundle_needed "$appdir/usr/lib" "$appdir/usr/bin/plotcore-egui-demo"
desktop_and_icon "$appdir" "$appimage_packaging/appimage/plotcore-egui-demo.desktop"
pack "$appdir" "$outdir/plotcore-egui-demo-$version-x86_64.AppImage"
