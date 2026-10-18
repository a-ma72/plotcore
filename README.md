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
  have to reproduce exactly (`tests/golden`).

## What is in here

| Path | What it is |
|---|---|
| `include/plotcore`, `src` | The C++ library, `plotcore::core`: the arithmetic (`Projection`, `Levels`, `Pyramid`, `Budget`), the frame and gestures (`Format`, `Ticks`, `Camera`, `Palettes`), the store (`Plot`), and `Figure`, the whole plot a host drives |
| `capi` | `Figure` as C, built as one shared library, `plotcore::c` |
| `qt` | A `QWidget` over a `Figure`, `plotcore::qt` (`PLOTCORE_WITH_QT`) |
| `python` | The Python package: ctypes over the C interface, with a PyQt6 widget and a matplotlib host |
| `rust` | The Rust workspace: `plotcore-sys`, the safe `plotcore` crate, `plotcore-egui` |
| `examples` | Every feature on one window, in Qt Widgets, PyQt6 and egui (the egui one is `rust/egui-demo`) |
| `packaging` | The demos as AppImages |

## Building

**C++ and C.** CMake 3.26 or newer and a C++20 compiler. Nothing else.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build
cmake --install build --prefix /where/it/goes   # find_package(plotcore)
```

Add `-DPLOTCORE_WITH_QT=ON` for the Qt widget and its example (Qt 6.5 or
newer).

**Python.** One wheel per platform serves every Python 3, because the package
links no Python:

```sh
pip install ./python              # or ./python[qt,matplotlib]
python examples/pyqt6/demo.py
```

To develop against a C library you built yourself, set `PLOTCORE_LIBRARY` to
its path.

**Rust.** The toolchain is pinned in `rust/rust-toolchain.toml`.

```sh
cd rust
cargo test --workspace
cargo run --release -p egui-demo
```

## Downloads

CI builds everything on every commit and uploads it, on Linux and Windows:

- the SDK (headers, the libraries and the CMake package)
- the wheel
- the Qt and egui demos for Windows
- all three demos as AppImages for Linux

The AppImages are built on AlmaLinux 8, so like H5Scope they start on RHEL 8
(glibc 2.28) and anything newer. Nothing is published to PyPI or crates.io yet.

## Licence

GPL-3.0-only. See [LICENSE](LICENSE).
