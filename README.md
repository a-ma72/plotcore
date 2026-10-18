<!--
SPDX-FileCopyrightText: 2026 Jonas Sattler
SPDX-License-Identifier: GPL-3.0-only
-->

# plotcore

The plot [H5Scope](https://github.com/jonassattler/H5Scope) draws, as a library
for C++, C, Python and Rust.

- **No spike is ever thinned away.** A line is held as a pyramid of min/max
  envelopes, and every extreme is drawn at the sample it is, so a one-sample
  spike in a billion survives at any zoom.
- **A zoom reads nothing.** Every picture is a fold of what is already held:
  the cost of a frame follows the width of the pane, not the length of the
  line.
- **The axes, gestures and colours are H5Scope's own,** ported function for
  function, and H5Scope's parity tests hold the two to each other. That
  includes logarithmic axes in any base, separate and stacked y axes, a flipped
  view, and the five colour cycles.
- **One plot in four languages.** A golden script of settings and gestures, and
  the digest of every scene it asks for, which the C, Python and Rust tests all
  have to reproduce exactly (`golden`).

## What is in here

One directory per language, and in each the library, its tests, and a demo
that walks every feature on one window.

| Path | What it is |
|---|---|
| `cpp` | The C++ library, `plotcore::core`: the arithmetic (`Projection`, `Levels`, `Pyramid`, `Budget`), the frame and gestures (`Format`, `Ticks`, `Camera`, `Palettes`), the store (`Plot`), and `Figure`, the whole plot a host drives. Beside it `qt`, a `QWidget` over a `Figure` (`plotcore::qt`), and `qt-demo` |
| `c` | `Figure` as C, built as one shared library, `plotcore::c`, which the Python and Rust libraries are built on |
| `python` | The Python package: ctypes over the C interface, with a PyQt6 widget and a matplotlib host. Beside it `pyqt6-demo` |
| `rust` | The Rust workspace: `plotcore-sys`, the safe `plotcore` crate, `plotcore-egui`, and `egui-demo` |
| `golden` | The script the C, Python and Rust tests all run, and the digest they must all write |
| `packaging` | The demos as AppImages for Linux (`appimage`) and as folders for Windows (`windows`) |

## Building

**C++ and C.** CMake 3.26 or newer and a C++20 compiler. Nothing else.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build
cmake --install build --prefix /where/it/goes   # find_package(plotcore)
```

Add `-DPLOTCORE_WITH_QT=ON` for the Qt widget and its demo (Qt 6.5 or newer),
which is then `build/cpp/qt-demo/plotcore-qt-demo`.

**Python.** One wheel per platform serves every Python 3, because the package
links no Python:

```sh
pip install ./python              # or ./python[qt,matplotlib]
pip install -r python/pyqt6-demo/requirements.txt
python python/pyqt6-demo/demo.py
```

To develop against a C library you built yourself, set `PLOTCORE_LIBRARY` to
its path.

**Rust.** The toolchain is pinned in `rust/rust-toolchain.toml`.

```sh
cd rust
cargo test --workspace
cargo run --release -p egui-demo
```

Every demo takes `--self-test`, which walks every control and gesture itself
and says whether each did what it should. CI runs all three that way.

## Downloads

CI builds everything on every commit, on Linux and Windows, and uploads it to
the run as artefacts:

| | Linux | Windows |
|---|---|---|
| The SDK: headers, libraries, CMake package | `plotcore-sdk-Linux` | `plotcore-sdk-Windows` |
| The Python wheel | `plotcore-python-Linux` | `plotcore-python-Windows` |
| The Qt demo | `plotcore-qt-demo-Linux` | `plotcore-qt-demo-Windows` |
| The egui demo | `plotcore-egui-demo-Linux` | `plotcore-egui-demo-Windows` |
| The PyQt6 demo | `plotcore-pyqt6-demo-Linux` | `plotcore-pyqt6-demo-Windows` |

A demo needs nothing installed. On Linux it is an AppImage: the download
loses its executable bit, so `chmod +x` it first. The AppImages are built on
AlmaLinux 8, so like H5Scope they start on RHEL 8 (glibc 2.28) and anything
newer. On Windows it is a folder with `plotcore-<toolkit>-demo.exe` in it.

Nothing is published to PyPI or crates.io yet.

## Licence

GPL-3.0-only. See [LICENSE](LICENSE).
