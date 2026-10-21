#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-License-Identifier: BSD-3-Clause
#
# The PyQt6 demo as an AppImage: a Python of its own with the wheel CI
# tested, PyQt6 and matplotlib installed into it, and the demo beside them.
#
# The Python is python-build-standalone's, which is built to be moved: no
# prefix compiled in, OpenSSL and the rest linked statically, glibc 2.17 the
# only thing it asks the host for. That is what makes it the right thing to
# put in an AppImage -- a distribution's Python knows where it was installed
# and goes looking there. Its releases are tags that are never rebuilt, so
# the archive is pinned by its hash.
#
# PyQt6's wheel carries its own Qt, plugins and all, found through its RPATHs.
# What it does not carry is what that Qt's xcb plugin links from the system:
# the xcb-util family and libxkbcommon-x11, and libxcb-cursor above all, which
# RHEL 8 ships only in EPEL -- without it, the window does not open on X11.
# Those are bundled into usr/lib and AppRun puts that on the library path.
#
# Usage: packaging/appimage/pyqt6-demo.sh <plotcore-wheel> <output-dir>

source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

if [ "$#" -ne 2 ]; then
    echo "usage: $0 <plotcore-wheel> <output-dir>" >&2
    exit 2
fi
wheel="$(readlink -f "$1")"
outdir="$2"
version="$(sed -n 's/^version = "\([0-9.]*\)"/\1/p' "$appimage_root/python/pyproject.toml" | head -1)"

PYTHON_URL="https://github.com/astral-sh/python-build-standalone/releases/download/20261003/cpython-3.12.15%2B20261003-x86_64-unknown-linux-gnu-install_only_stripped.tar.gz"
PYTHON_SHA256="731af898886c5f821890dc901eca3c651cca8e51fa7308c159d12a1194aeac91"

echo "plotcore $version PyQt6 demo -> AppImage"

fetch "$PYTHON_URL" "$PYTHON_SHA256" "$appimage_tools/cpython-3.12.15+20261003.tar.gz"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
appdir="$work/plotcore-pyqt6-demo.AppDir"
mkdir -p "$appdir/opt"
tar -xzf "$appimage_tools/cpython-3.12.15+20261003.tar.gz" -C "$appdir/opt"
python="$appdir/opt/python/bin/python3.12"

# -I: nothing from the build machine's environment or user site decides what
# goes in.
"$python" -I -m pip install --no-cache-dir --no-warn-script-location --disable-pip-version-check \
    -r "$appimage_root/python/pyqt6-demo/requirements.txt" "$wheel"

install -Dm 644 "$appimage_root/python/pyqt6-demo/demo.py" \
    "$appdir/usr/share/plotcore/pyqt6/demo.py"
install -Dm 644 "$appimage_root/python/pyqt6-demo/signals.py" \
    "$appdir/usr/share/plotcore/pyqt6/signals.py"

cat > "$appdir/AppRun" <<'EOF'
#!/bin/sh
# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-License-Identifier: BSD-3-Clause
#
# -I, so that a PYTHONPATH or a user site on the host cannot put a different
# numpy or PyQt6 under the demo than the one it was packed with.
APPDIR="$(dirname "$(readlink -f "$0")")"
export LD_LIBRARY_PATH="$APPDIR/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$APPDIR/opt/python/bin/python3.12" -I "$APPDIR/usr/share/plotcore/pyqt6/demo.py" "$@"
EOF
chmod 755 "$appdir/AppRun"

qt6="$("$python" -I -c 'import PyQt6, pathlib; print(pathlib.Path(PyQt6.__file__).parent / "Qt6")')"
bundle_needed "$appdir/usr/lib" \
    "$qt6"/plugins/platforms/*.so \
    "$qt6"/plugins/xcbglintegrations/*.so \
    "$qt6"/lib/libQt6XcbQpa.so.6

desktop_and_icon "$appdir" "$appimage_packaging/appimage/plotcore-pyqt6-demo.desktop"
qt_licences "$appdir"
pack "$appdir" "$outdir/plotcore-pyqt6-demo-$version-x86_64.AppImage"
