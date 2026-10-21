#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-License-Identifier: BSD-3-Clause
#
# Prove that every ELF file given will load on the oldest distribution the
# AppImages are for, without having that distribution to hand. H5Scope's
# tools/check-glibc-floor.sh, with one difference: those programs link the
# C++ runtime dynamically (a prebuilt Qt, PyQt6's wheel), so a reference to
# libstdc++ is not an error here -- a reference to one newer than the host's
# is.
#
#   GLIBC_*            the glibc the host must have; RHEL 8 has 2.28
#   GLIBCXX_*/CXXABI_* the libstdc++ it must have; RHEL 8's is GCC 8's,
#                      3.4.25 and 1.3.11
#
# Only a failure is printed per file, because the PyQt6 image alone is a few
# hundred of them.
#
# Usage: packaging/appimage/check-floor.sh <glibc> <glibcxx> <cxxabi> <file>...
#    eg: packaging/appimage/check-floor.sh 2.28 3.4.25 1.3.11 squashfs-root/usr/bin/*

set -euo pipefail

if [ "$#" -lt 4 ]; then
    echo "usage: $0 <glibc> <glibcxx> <cxxabi> <file>..." >&2
    exit 2
fi
glibc_floor="$1" glibcxx_floor="$2" cxxabi_floor="$3"
shift 3

# sort -V orders 2.9 before 2.10, which a lexical sort gets backwards.
version_gt() {
    [ "$1" != "$2" ] && [ "$(printf '%s\n%s\n' "$1" "$2" | sort -V | tail -1)" = "$1" ]
}

highest() {
    grep -oE "$1_[0-9]+(\.[0-9]+)+" | sed "s/^$1_//" | sort -uV | tail -1 || true
}

status=0 checked=0
worst_glibc="0" worst_glibcxx="0" worst_cxxabi="0"
for file in "$@"; do
    [ -f "$file" ] || continue
    # ELF files only: an AppDir is mostly Python, fonts and pictures.
    [ "$(head -c 4 "$file" | od -An -c | tr -d ' ')" = "177ELF" ] || continue
    checked=$((checked + 1))
    symbols="$(objdump -T "$file" 2>/dev/null || true)"
    for kind in GLIBC:glibc_floor:worst_glibc GLIBCXX:glibcxx_floor:worst_glibcxx \
                CXXABI:cxxabi_floor:worst_cxxabi; do
        IFS=: read -r prefix floor_var worst_var <<<"$kind"
        need="$(printf '%s\n' "$symbols" | highest "$prefix")"
        [ -n "$need" ] || continue
        version_gt "$need" "${!worst_var}" && printf -v "$worst_var" '%s' "$need"
        if version_gt "$need" "${!floor_var}"; then
            echo "::error::$file needs ${prefix}_$need, above the floor of ${!floor_var}"
            status=1
        fi
    done
done

echo "checked $checked ELF files: glibc up to $worst_glibc (floor $glibc_floor)," \
     "libstdc++ up to $worst_glibcxx/$worst_cxxabi (floor $glibcxx_floor/$cxxabi_floor)"
exit "$status"
