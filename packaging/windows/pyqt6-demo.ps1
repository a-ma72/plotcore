# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-License-Identifier: GPL-3.0-only
#
# The PyQt6 demo for Windows: the wheel CI tested, PyQt6 and matplotlib, and
# the demo, frozen by PyInstaller into a folder with a windowed executable.
#
# The AppImage puts a Python together by hand, because it has a glibc floor to
# hold and a relocatable Python to hold it with. Windows has no floor of that
# kind, and PyInstaller is the tool made for the rest: it follows the demo's
# imports -- signals.py beside it, plotcore, matplotlib's Qt backend -- and
# its hooks know what PyQt6, numpy and matplotlib load besides. What no
# import names is plotcore's own library, which _native.py opens with ctypes
# from beside itself, so that is collected by name.
#
# The demo's pins (python/pyqt6-demo/requirements.txt), which the AppImage
# carries too, and PyInstaller's own, so that the bundle does not change
# under a commit. Built with the Python first on the PATH -- 3.12 in CI, as
# the AppImage's is.
#
# Usage: pwsh packaging/windows/pyqt6-demo.ps1 <plotcore-wheel> <output-dir>

param(
    [Parameter(Mandatory)] [string] $Wheel,
    [Parameter(Mandatory)] [string] $OutDir
)

. "$PSScriptRoot/common.ps1"

$pyinstaller = @('pyinstaller==6.22.3', 'pyinstaller-hooks-contrib==2026.8')

$wheelPath = (Resolve-Path $Wheel).Path
New-Item -ItemType Directory -Force $OutDir | Out-Null
$dist = (Resolve-Path $OutDir).Path
$demo = "$root/python/pyqt6-demo"

Write-Host "plotcore $version PyQt6 demo -> $OutDir"

$work = Join-Path ([IO.Path]::GetTempPath()) "plotcore-pyqt6-demo-$PID"
try {
    python -m venv "$work/venv"
    $python = "$work/venv/Scripts/python.exe"
    # -I: nothing from the build machine's environment or user site decides
    # what goes in.
    & $python -I -m pip install --no-cache-dir --disable-pip-version-check `
        -r "$demo/requirements.txt" $wheelPath @pyinstaller
    # PyInstaller follows what a DLL imports but not what it delay-loads, and
    # the msvcp140.dll PyQt6 ships delay-loads concrt140.dll: the wheel carries
    # it beside msvcp140.dll, and the bundle left it out until verify.ps1 said
    # so. Collected by name, beside the Visual C++ runtime PyInstaller
    # collects for Python itself.
    $concrt = & $python -I -c `
        "import pathlib, PyQt6; print(pathlib.Path(PyQt6.__file__).parent / 'Qt6' / 'bin' / 'concrt140.dll')"
    # The icon is a PNG, which PyInstaller converts with Pillow -- installed
    # already, because matplotlib needs it.
    & $python -I -m PyInstaller --noconfirm --clean --log-level WARN `
        --windowed `
        --name plotcore-pyqt6-demo `
        --icon "$root/packaging/plotcore.png" `
        --collect-binaries plotcore `
        --add-binary "$concrt;." `
        --distpath $dist `
        --workpath "$work/build" `
        --specpath $work `
        "$demo/demo.py"
} finally {
    Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
}

Complete-Bundle "$dist/plotcore-pyqt6-demo"
