# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-License-Identifier: GPL-3.0-only
#
# The egui demo for Windows: one executable.
#
# Rust links its own code statically and plotcore's C++ through plotcore-sys,
# which leaves the C runtime, and +crt-static links that in too: the cc crate
# reads the same flag and compiles plotcore with /MT to match. What is left
# for Windows to provide is Windows' own -- user32, opengl32 and the like --
# so unlike the Qt demo there is nothing to deploy beside it.
#
# Usage: pwsh packaging/windows/egui-demo.ps1 <output-dir>

param(
    [Parameter(Mandatory)] [string] $OutDir
)

. "$PSScriptRoot/common.ps1"

Write-Host "plotcore $version egui demo -> $OutDir"

$env:RUSTFLAGS = '-C target-feature=+crt-static'
# From rust/, where rust-toolchain.toml names the toolchain.
Push-Location "$root/rust"
try {
    cargo build --release --locked -p egui-demo
} finally {
    Pop-Location
}

$out = New-Bundle 'plotcore-egui-demo' $OutDir
Copy-Item "$root/rust/target/release/egui-demo.exe" "$out/plotcore-egui-demo.exe"
Complete-Bundle $out
