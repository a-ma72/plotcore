# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-License-Identifier: GPL-3.0-only
# shellcheck shell=bash
#
# What the three demo AppImages share: the pinned tools, the libraries left to
# the host, the walk that bundles the rest, and the packing. Sourced, not run.
#
# This is H5Scope's tools/make-appimage.sh taken apart, because H5Scope packs
# one executable and this packs three different kinds of thing -- a Qt program
# with its plugins, a Rust program that needs nothing, and a Python
# interpreter with a whole package tree. What they have in common is
# everything after the AppDir is laid out, and that is what is here.
#
# Each AppImage is built inside manylinux_2_28 (AlmaLinux 8, glibc 2.28), the
# image the wheel is built in, so that it starts on RHEL 8 like H5Scope does.
# An AppImage carries libraries, not a loader: what is inside it still has to
# have been built against a glibc no newer than the host's, and
# tools/check-floor.sh is what says so.

set -euo pipefail

readonly appimage_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
readonly appimage_packaging="$appimage_root/packaging"

# --- pinned tools ----------------------------------------------------------
# H5Scope's pins, for H5Scope's reasons: appimagetool would otherwise fetch an
# unpinned runtime into the first kilobytes of every image, and this runtime
# is static and mounts through FUSE 2, which is the one RHEL 8 has.
APPIMAGETOOL_URL="https://github.com/AppImage/appimagetool/releases/download/1.9.1/appimagetool-x86_64.AppImage"
APPIMAGETOOL_SHA256="ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0"

RUNTIME_URL="https://github.com/AppImage/type2-runtime/releases/download/20251108/runtime-x86_64"
RUNTIME_SHA256="2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d"

# Only the Qt demo needs these: linuxdeploy and its Qt plugin are what know
# which of a Qt's plugins a program needs and where they go. Tagged releases,
# not "continuous", which is rebuilt under the same name.
LINUXDEPLOY_URL="https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20251107-1/linuxdeploy-x86_64.AppImage"
LINUXDEPLOY_SHA256="c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d"

LINUXDEPLOY_QT_URL="https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/1-alpha-20250213-1/linuxdeploy-plugin-qt-x86_64.AppImage"
LINUXDEPLOY_QT_SHA256="15106be885c1c48a021198e7e1e9a48ce9d02a86dd0a1848f00bdbf3c1c92724"

appimage_tools="${PLOTCORE_APPIMAGE_TOOLS:-${XDG_CACHE_HOME:-$HOME/.cache}/plotcore/appimage-tools}"

# Every tool here is itself an AppImage, and there is no FUSE inside a build
# container.
export APPIMAGE_EXTRACT_AND_RUN=1

fetch() {
    local url="$1" sha="$2" dest="$3"
    mkdir -p "$(dirname "$dest")"
    if [ -f "$dest" ] && echo "$sha  $dest" | sha256sum --check --status; then
        echo "  cached: $(basename "$dest")"
        return
    fi
    echo "  fetching: $(basename "$dest")"
    curl -sSL --fail --retry 3 --retry-delay 5 -o "$dest.tmp" "$url"
    if ! echo "$sha  $dest.tmp" | sha256sum --check --status; then
        echo "error: checksum mismatch for $url" >&2
        echo "  expected $sha" >&2
        echo "  got      $(sha256sum "$dest.tmp" | cut -d' ' -f1)" >&2
        rm -f "$dest.tmp"
        exit 1
    fi
    mv "$dest.tmp" "$dest"
    chmod +x "$dest"
}

# --- what stays on the host ------------------------------------------------
# H5Scope's list. Matched against the soname, anchored at both ends, so that
# libxcb.so.1 is left to the host without libxcb-cursor.so.0 going with it.
readonly -a HOST_LIBRARIES=(
    # glibc and the loader.
    'ld-linux-x86-64\.so\..*' 'libc\.so\..*' 'libm\.so\..*' 'libdl\.so\..*'
    'libpthread\.so\..*' 'librt\.so\..*' 'libresolv\.so\..*' 'libnsl\.so\..*'
    'libutil\.so\..*' 'libcrypt\.so\..*' 'libanl\.so\..*'
    # The C++ runtime. Unlike H5Scope's, these programs link it dynamically --
    # a prebuilt Qt does -- and gcc-toolset builds against the host's
    # libstdc++ plus a static archive of what is newer, which is how a
    # program built with GCC 14 runs on RHEL 8's GCC 8 runtime.
    'libstdc\+\+\.so\..*' 'libgcc_s\.so\..*'
    # glvnd and the driver stack: these dispatch into whatever GPU driver the
    # host has, and a bundled copy sends every NVIDIA user to a crash.
    'libGL\.so\..*' 'libGLX\.so\..*' 'libGLdispatch\.so\..*' 'libOpenGL\.so\..*'
    'libEGL\.so\..*' 'libGLESv[12]\.so\..*' 'libglapi\.so\..*'
    'libdrm\.so\..*' 'libgbm\.so\..*'
    # The core X protocol libraries, coupled to the running server and to the
    # GL stack above. The xcb-util family layered on top is not, and is
    # bundled -- libxcb-cursor above all, which RHEL 8 has only in EPEL.
    'libX11\.so\..*' 'libX11-xcb\.so\..*' 'libxcb\.so\..*' 'libxcb-glx\.so\..*'
    # Coupled to the compositor and, for -egl, to the driver.
    'libwayland-client\.so\..*' 'libwayland-server\.so\..*' 'libwayland-egl\.so\..*'
    'libwayland-cursor\.so\..*'
    # Coupled to the host's fonts and their configuration, which is the point
    # of reading them.
    'libfontconfig\.so\..*' 'libfreetype\.so\..*'
)

is_host_library() {
    local soname="$1" pattern
    for pattern in "${HOST_LIBRARIES[@]}"; do
        [[ "$soname" =~ ^${pattern}$ ]] && return 0
    done
    return 1
}

# bundle_needed <libdir> <elf>...
#
# A breadth-first walk over DT_NEEDED, copying into <libdir> every library the
# given files need that the host does not provide and that is not already
# inside the AppDir. H5Scope's walk, for its reason: ldd's whole closure would
# drag in everything reachable through libraries deliberately left to the
# host. What ldd is for is where each soname resolves.
bundle_needed() {
    local libdir="$1"
    shift
    mkdir -p "$libdir"
    local appdir
    appdir="$(cd "$libdir/../.." && pwd)"

    declare -A resolved=()
    local elf soname arrow path rest
    for elf in "$@"; do
        while read -r soname arrow path rest; do
            [ "$arrow" = "=>" ] || continue
            [ -n "$path" ] && [ -e "$path" ] || continue
            [ -n "${resolved[$soname]:-}" ] || resolved["$soname"]="$path"
        done < <(ldd "$elf" 2>/dev/null | sed 's/^[[:space:]]*//')
    done

    declare -A seen=()
    local -a queue=() bundled=()
    for elf in "$@"; do
        while read -r soname; do
            [ -n "$soname" ] && queue+=("$soname")
        done < <(objdump -p "$elf" 2>/dev/null | awk '/NEEDED/ {print $2}')
    done

    while [ "${#queue[@]}" -gt 0 ]; do
        soname="${queue[0]}"
        queue=("${queue[@]:1}")
        [ -n "${seen[$soname]:-}" ] && continue
        seen["$soname"]=1
        is_host_library "$soname" && continue

        path="${resolved[$soname]:-}"
        if [ -z "$path" ]; then
            echo "error: $soname is needed but ldd could not find it" >&2
            exit 1
        fi
        # Already in the AppDir -- a wheel's own Qt, found through its RPATH.
        case "$(readlink -f "$path")" in "$appdir"/*) continue ;; esac

        install -m 644 "$(readlink -f "$path")" "$libdir/$soname"
        bundled+=("$soname")
        while read -r dependency; do
            [ -n "$dependency" ] && queue+=("$dependency")
        done < <(objdump -p "$path" 2>/dev/null | awk '/NEEDED/ {print $2}')
        # What this library needs resolves where ldd says, which it was not
        # asked about before.
        while read -r soname arrow path rest; do
            [ "$arrow" = "=>" ] || continue
            [ -n "$path" ] && [ -e "$path" ] || continue
            [ -n "${resolved[$soname]:-}" ] || resolved["$soname"]="$path"
        done < <(ldd "$libdir/${bundled[-1]}" 2>/dev/null | sed 's/^[[:space:]]*//')
    done

    if [ "${#bundled[@]}" -eq 0 ]; then
        echo "  bundled no libraries"
    else
        echo "  bundled ${#bundled[@]} libraries:"
        printf '    %s\n' "${bundled[@]}" | sort
    fi
}

# unbundle_host_libraries <libdir>
#
# linuxdeploy keeps an exclusion list of its own, and it is not this one: it
# bundles libxcb-glx and the Wayland client's EGL and cursor libraries. One
# list decides for all three images, so whatever it took that this list leaves
# to the host is taken back out.
unbundle_host_libraries() {
    local libdir="$1" path
    for path in "$libdir"/*.so*; do
        [ -e "$path" ] || continue
        if is_host_library "$(basename "$path")"; then
            echo "  left to the host: $(basename "$path")"
            rm -f "$path"
        fi
    done
}

# desktop_and_icon <appdir> <desktop-file>
#
# appimagetool wants a .desktop and its icon at the AppDir root; a desktop
# that integrates the image reads the copies under usr/share.
desktop_and_icon() {
    local appdir="$1" desktop="$2"
    local name
    name="$(basename "$desktop")"
    install -Dm 644 "$desktop" "$appdir/usr/share/applications/$name"
    ln -sf "usr/share/applications/$name" "$appdir/$name"
    install -Dm 644 "$appimage_packaging/plotcore.png" \
        "$appdir/usr/share/icons/hicolor/256x256/apps/plotcore.png"
    install -Dm 644 "$appimage_packaging/plotcore.svg" \
        "$appdir/usr/share/icons/hicolor/scalable/apps/plotcore.svg"
    install -m 644 "$appimage_packaging/plotcore.png" "$appdir/plotcore.png"
    cp "$appimage_packaging/plotcore.png" "$appdir/.DirIcon"
    # Each image is a conveyance of GPL-3.0-only object code, so it carries
    # the licence (section 4).
    install -Dm 644 "$appimage_root/LICENSE" "$appdir/usr/share/doc/plotcore/LICENSE"
}

# pack <appdir> <output>
pack() {
    local appdir="$1" output="$2"
    fetch "$APPIMAGETOOL_URL" "$APPIMAGETOOL_SHA256" "$appimage_tools/appimagetool"
    fetch "$RUNTIME_URL" "$RUNTIME_SHA256" "$appimage_tools/runtime-x86_64"
    mkdir -p "$(dirname "$output")"
    rm -f "$output"
    # --no-appstream: there is no AppStream metadata to validate; the .desktop
    # file is still checked.
    ARCH=x86_64 "$appimage_tools/appimagetool" \
        --runtime-file "$appimage_tools/runtime-x86_64" \
        --no-appstream \
        "$appdir" "$output"
    chmod +x "$output"
    echo "wrote $output ($(du -h "$output" | cut -f1))"
}
