# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-License-Identifier: BSD-3-Clause
#
# Everything that has to be true of a Windows demo bundle, as verify.sh is for
# an AppImage. A folder that was produced is not one that starts, and the way
# it is built wrong is quiet: a DLL every runner has and a fresh Windows does
# not.
#
# Usage: pwsh packaging/windows/verify.ps1 <bundle-dir>
#    eg: pwsh packaging/windows/verify.ps1 dist/plotcore-qt-demo

param(
    [Parameter(Mandatory)] [string] $Bundle
)

. "$PSScriptRoot/common.ps1"

$bundlePath = (Resolve-Path $Bundle).Path
$name = Split-Path $bundlePath -Leaf
$exe = Join-Path $bundlePath "$name.exe"
$size = (Get-ChildItem $bundlePath -Recurse -File | Measure-Object Length -Sum).Sum / 1MB
Write-Host ('== {0} ({1:N0} MB)' -f $name, $size)

# Found before the PATH is taken away below.
$dumpbin = & $vswhere -latest -find 'VC/Tools/MSVC/*/bin/Hostx64/x64/dumpbin.exe' |
    Select-Object -First 1
if (-not $dumpbin) {
    throw 'there is no dumpbin to read the imports with'
}

# Every Visual C++ runtime DLL that anything in the bundle imports is in the
# bundle. Asked of the files rather than of a run, because the runner has the
# redistributable installed and would start a bundle that carries none of it.
# Matched by prefix, so that a copy a wheel renamed (msvcp140-<hash>.dll) is
# matched against itself. What a file delay-loads counts as well: dumpbin
# lists it with the rest, and it is how the PyQt6 bundle was found without
# the concrt140.dll its msvcp140.dll delay-loads, which PyInstaller does not
# follow.
Write-Host '-- the C++ runtime it needs, it carries'
$runtime = '^(vcruntime|msvcp|concrt|vccorlib|vcomp)[^\\/]*\.dll$'
$carried = @{}
Get-ChildItem $bundlePath -Recurse -File -Filter *.dll | ForEach-Object {
    $carried[$_.Name.ToLowerInvariant()] = $true
}
$missing = @()
$read = 0
foreach ($file in Get-ChildItem $bundlePath -Recurse -File -Include *.exe, *.dll, *.pyd) {
    $read++
    $imports = & $dumpbin /nologo /dependents $file.FullName |
        ForEach-Object { $_.Trim() } |
        Where-Object { $_ -match $runtime }
    foreach ($import in $imports) {
        if (-not $carried.ContainsKey($import.ToLowerInvariant())) {
            $missing += "$($file.FullName.Substring($bundlePath.Length + 1)) imports $import"
        }
    }
}
if ($missing.Count -gt 0) {
    $missing | ForEach-Object { Write-Host "::error::$_, which is not in the bundle" }
    throw "$name does not carry the C++ runtime it needs"
}
Write-Host "read the imports of $read files: every runtime DLL they name is carried"

# plotcore's licence, and where the bundle carries Qt, the LGPL-3.0 it is
# conveyed under and the GPL-3.0 (PyQt6's too): plotcore's own says nothing
# of them.
Write-Host '-- the licences of what it carries'
$texts = @('LICENSE')
if ($carried.ContainsKey('qt6core.dll')) {
    $texts += 'licenses/README.md', 'licenses/LGPL-3.0.txt', 'licenses/GPL-3.0.txt'
}
foreach ($text in $texts) {
    if (-not (Test-Path (Join-Path $bundlePath $text) -PathType Leaf)) {
        throw "$name does not carry $text"
    }
}
Write-Host "it carries $($texts -join ', ')"

# --self-test from the bundle alone: Qt, Python and every toolchain off the
# PATH, nothing pointing Qt or Python anywhere else, offscreen. Through
# Start-Process, because each demo is a windowed program and nothing else
# waits for one to exit.
Write-Host '-- it walks every feature itself'
$env:PATH = "$env:SystemRoot\System32;$env:SystemRoot;$env:SystemRoot\System32\Wbem"
foreach ($variable in 'QT_PLUGIN_PATH', 'QT_ROOT_DIR', 'QML2_IMPORT_PATH', 'PYTHONPATH', 'PYTHONHOME') {
    Remove-Item "Env:$variable" -ErrorAction SilentlyContinue
}
$env:QT_QPA_PLATFORM = 'offscreen'
$scratch = [IO.Path]::GetTempPath()
$picture = Join-Path $scratch "$name.png"
$log = Join-Path $scratch "$name.log"
$process = Start-Process -FilePath $exe -ArgumentList '--self-test', "`"$picture`"" `
    -WorkingDirectory $scratch -NoNewWindow -Wait -PassThru -RedirectStandardOutput $log
$output = @(Get-Content $log -ErrorAction SilentlyContinue)
if ($process.ExitCode -ne 0) {
    $output | Write-Host
    throw "$name --self-test exited with $($process.ExitCode)"
}
$output | Select-Object -Last 1 | Write-Host
