# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-License-Identifier: GPL-3.0-only
#
# The Qt Widgets demo for Windows.
#
# A prebuilt Qt is DLLs and plugins, and windeployqt is what knows which of
# them a program loads and where they go, as linuxdeploy's Qt plugin does for
# the AppImage. Two things it leaves out are added by hand: the offscreen
# platform plugin, because --self-test runs on it and Qt reports a missing
# platform in a message box that nothing on a runner dismisses; and the Visual
# C++ runtime, which Qt and the demo link dynamically.
#
# Qt 6.8, as for the AppImage, so that the two are one demo.
#
# Usage: pwsh packaging/windows/qt-demo.ps1 <qt-prefix> <output-dir>
#    eg: pwsh packaging/windows/qt-demo.ps1 C:/Qt/6.8.3/msvc2022_64 dist

param(
    [Parameter(Mandatory)] [string] $QtPrefix,
    [Parameter(Mandatory)] [string] $OutDir
)

. "$PSScriptRoot/common.ps1"

$qt = (Resolve-Path $QtPrefix).Path
$build = Join-Path $root 'build/windows-qt'

Write-Host "plotcore $version Qt demo -> $OutDir"

cmake -S $root -B $build "-DCMAKE_PREFIX_PATH=$qt" `
    -DPLOTCORE_WITH_QT=ON `
    -DPLOTCORE_BUILD_TESTS=OFF `
    -DPLOTCORE_BUILD_CAPI=OFF `
    -DPLOTCORE_INSTALL=OFF
cmake --build $build --config Release --target plotcore-qt-demo --parallel

$out = New-Bundle 'plotcore-qt-demo' $OutDir
Copy-Item "$build/cpp/qt-demo/Release/plotcore-qt-demo.exe" $out
& "$qt/bin/windeployqt.exe" --release --no-translations --no-system-d3d-compiler --no-opengl-sw `
    "$out/plotcore-qt-demo.exe"
Copy-Item "$qt/plugins/platforms/qoffscreen.dll" "$out/platforms/"
Copy-VCRuntime $out
Complete-Bundle $out
