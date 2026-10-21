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
# All of Qt is more than the demo loads, and is cut down to what it does (see
# trim_pyqt6 below).
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

# The archive above carries CPython's licence and not those of what is
# linked into it -- OpenSSL, SQLite, libffi, ncurses and the rest. The same
# release's full archives carry those, and the smallest of them is fetched
# for nothing else.
PYTHON_LICENCES_URL="https://github.com/astral-sh/python-build-standalone/releases/download/20261003/cpython-3.12.15%2B20261003-x86_64-unknown-linux-gnu-debug-full.tar.zst"
PYTHON_LICENCES_SHA256="6cb82cf06cf9ee1a89e73b17e91fc5e19bc1a1f83ff2662a620b02429671f808"

echo "plotcore $version PyQt6 demo -> AppImage"

fetch "$PYTHON_URL" "$PYTHON_SHA256" "$appimage_tools/cpython-3.12.15+20261003.tar.gz"
fetch "$PYTHON_LICENCES_URL" "$PYTHON_LICENCES_SHA256" \
    "$appimage_tools/cpython-3.12.15+20261003-debug-full.tar.zst"

# trim_pyqt6 <PyQt6-package>
#
# PyQt6-Qt6 is all of Qt -- a hundred modules, QML, Qt Quick 3D, Qt PDF with
# pdfium, Qt Multimedia with a build of FFmpeg -- and the demo imports
# QtCore, QtGui and QtWidgets, and matplotlib's Qt backend QtSvg besides. An
# image owes notices for everything it carries and source for the copyleft
# part of it, so it carries those four and what they and the plugins kept
# below load, as the Windows build does by PyInstaller's analysis of the
# imports.
# What is left is Qt's qtbase, qtsvg and qtwayland, whose notices are kept
# under packaging/licenses/pyqt6-qt6/; the image formats are the ones those
# build, and the platforms X11, Wayland and the offscreen one --self-test
# runs on.
trim_pyqt6() {
    local pyqt="$1" qt="$1/Qt6" path soname
    for path in "$pyqt"/Qt*.abi3.so; do
        case "$(basename "$path")" in
            QtCore.abi3.so | QtGui.abi3.so | QtWidgets.abi3.so | QtSvg.abi3.so) ;;
            *) rm -f "$path" "${path%.abi3.so}.pyi" ;;
        esac
    done
    for path in "$qt"/plugins/*; do
        case "$(basename "$path")" in
            platforms)
                find "$path" -type f ! \( -name libqxcb.so -o -name 'libqwayland-*.so' \
                    -o -name libqoffscreen.so \) -delete ;;
            imageformats)
                find "$path" -type f ! \( -name libqgif.so -o -name libqico.so \
                    -o -name libqjpeg.so -o -name libqsvg.so \) -delete ;;
            platforminputcontexts | platformthemes | xcbglintegrations | iconengines | wayland-*) ;;
            *) rm -rf "$path" ;;
        esac
    done
    rm -rf "$qt/qml" "$qt/qsci"

    # Qt6/lib keeps what the modules and plugins above need, transitively.
    local -a queue=() roots=()
    declare -A keep=()
    mapfile -t roots < <(find "$pyqt" -maxdepth 1 -name 'Qt*.abi3.so'; find "$qt/plugins" -name '*.so')
    mapfile -t queue < <(objdump -p "${roots[@]}" | awk '/NEEDED/ {print $2}')
    while [ "${#queue[@]}" -gt 0 ]; do
        soname="${queue[0]}"
        queue=("${queue[@]:1}")
        if [ -n "${keep[$soname]:-}" ] || [ ! -e "$qt/lib/$soname" ]; then
            continue
        fi
        keep["$soname"]=1
        mapfile -t -O "${#queue[@]}" queue < <(objdump -p "$qt/lib/$soname" | awk '/NEEDED/ {print $2}')
    done
    for path in "$qt"/lib/*; do
        [ -n "${keep[$(basename "$path")]:-}" ] || rm -rf "$path"
    done
    echo "  kept ${#keep[@]} of Qt's libraries:"
    printf '    %s\n' "${!keep[@]}" | sort
}

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

pyqt="$("$python" -I -c 'import PyQt6, pathlib; print(pathlib.Path(PyQt6.__file__).parent)')"
qt6="$pyqt/Qt6"
trim_pyqt6 "$pyqt"
bundle_needed "$appdir/usr/lib" \
    "$qt6"/plugins/platforms/*.so \
    "$qt6"/plugins/xcbglintegrations/*.so \
    "$qt6"/lib/libQt6XcbQpa.so.6

desktop_and_icon "$appdir" "$appimage_packaging/appimage/plotcore-pyqt6-demo.desktop"
licences "$appdir"
qt_licences "$appdir"
# The Python section is written by the image's own Python, which is how it
# lists what that Python has installed and nothing of the build machine's.
python_licences="$work/python-licences"
mkdir -p "$python_licences"
zstd -dc "$appimage_tools/cpython-3.12.15+20261003-debug-full.tar.zst" \
    | tar -x -C "$python_licences" --strip-components=2 python/licenses
"$python" -I -B "$appimage_packaging/notices.py" python "$appdir/$licences_dir" \
    --interpreter-licences "$python_licences"
qt_version="$("$python" -I -c 'import importlib.metadata as m; print(m.version("PyQt6-Qt6"))')"
mapfile -t qt_binaries < <(find "$qt6/lib" "$qt6/plugins" -name '*.so*')
notices qt "$appdir/$licences_dir" "$appimage_packaging/licenses/pyqt6-qt6/$qt_version-linux" \
    "${qt_binaries[@]}"
system_notices "$appdir" "$outdir" "$version"
pack "$appdir" "$outdir/plotcore-pyqt6-demo-$version-x86_64.AppImage"
