<!--
SPDX-FileCopyrightText: 2026 Jonas Sattler
SPDX-License-Identifier: BSD-3-Clause
-->

# The licences of what this package carries

plotcore is under the BSD 3-Clause licence, in `LICENSE` beside this folder.
Everything else in the package is someone else's, and this folder holds its
licences, one section per kind of thing a package takes from elsewhere:

- `rust/` -- the Rust crates the egui demo is compiled from, and the Rust
  standard library.
- `python/` -- the Python the PyQt6 demo runs on, and every distribution
  installed beside it.
- `qt/` -- the third-party code Qt builds into the libraries and plugins the
  package carries, and the ICU some of them use.
- `system/` -- in an AppImage, the libraries it takes from the AlmaLinux 8 it
  was built on, each under the package that installed it.

Each section's `index.md` lists what it covers, with its version, its licence
and the files that hold the licence's text and the copyright lines.

## Qt, PyQt6 and the source

The Qt and PyQt6 demos carry Qt, which they convey under the GNU Lesser
General Public License 3.0 (`LGPL-3.0.txt`): the GNU General Public License
3.0 (`GPL-3.0.txt`) with the permissions it adds. The PyQt6 demo also
carries PyQt6, which is under the GPL-3.0 alone, so that demo is distributed
under the GPL-3.0 as a whole.

Where a licence asks for the source to be offered, the sections say where it
is: Qt's at download.qt.io for the version carried, PyQt6's and every other
distribution's on PyPI, and that of every system library in an AppImage
published beside the image on its release page, as the source RPMs it was
built from.
