# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-License-Identifier: BSD-3-Clause
#
# What the three Windows demo bundles share. Dot-sourced, not run.
#
# A bundle is the Windows counterpart of an AppImage: a folder,
# plotcore-<toolkit>-demo, holding plotcore-<toolkit>-demo.exe, everything it
# loads that a fresh Windows does not have, and the licences. verify.ps1 holds
# each to that.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
# A native command that fails stops the script, as set -e does in bash.
$PSNativeCommandUseErrorActionPreference = $true

$root = (Resolve-Path "$PSScriptRoot/../..").Path
$version = (Select-String -Path "$root/CMakeLists.txt" -Pattern '^project\(plotcore VERSION ([0-9.]+)').Matches[0].Groups[1].Value
$vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"

# An empty folder named for the bundle, under the output directory.
function New-Bundle([string] $Name, [string] $OutDir) {
    $out = Join-Path $OutDir $Name
    if (Test-Path $out) {
        Remove-Item -Recurse -Force $out
    }
    New-Item -ItemType Directory -Force $out | Out-Null
    return (Resolve-Path $out).Path
}

# The Visual C++ runtime, from the Visual Studio that built what links it
# dynamically. A machine without the redistributable has none of it, and a
# runner has all of it, which is why verify.ps1 reads the imports rather than
# trusting that the bundle started.
function Copy-VCRuntime([string] $Out) {
    $crt = & $vswhere -latest -find 'VC/Redist/MSVC/*/x64/Microsoft.VC*.CRT' | Select-Object -Last 1
    if (-not $crt) {
        throw 'there is no Visual C++ runtime to deploy'
    }
    Copy-Item "$crt/*.dll" $Out
}

# Qt is conveyed under the LGPL-3.0, which asks for its own text and the
# GPL-3.0's beside the work it is part of (section 4), and PyQt6 under the
# GPL-3.0 alone. plotcore's licence says nothing of either, so a bundle
# carrying Qt carries both texts, and a word on which is whose.
function Copy-QtLicences([string] $Out) {
    $licences = New-Item -ItemType Directory -Force (Join-Path $Out 'licenses')
    Copy-Item "$root/packaging/licenses/*" $licences
}

# Each bundle is plotcore in binary form, so it carries the licence (its
# second condition).
function Complete-Bundle([string] $Out) {
    Copy-Item "$root/LICENSE" $Out
    $files = @(Get-ChildItem $Out -Recurse -File)
    $size = ($files | Measure-Object Length -Sum).Sum / 1MB
    Write-Host ('wrote {0} ({1:N0} MB in {2} files)' -f $Out, $size, $files.Count)
}
