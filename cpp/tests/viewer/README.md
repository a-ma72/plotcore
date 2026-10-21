<!--
SPDX-FileCopyrightText: 2026 Jonas Sattler
SPDX-License-Identifier: BSD-3-Clause
-->

# The viewer's answers

`Format`, `Ticks`, `Camera` and `Palettes` are ports of H5Scope's QML:
`PlotFrame.qml`'s numbering and ruling, `PlotSurface.qml`'s zoom and pan, and
`Theme.qml`'s colours. H5Scope's `tests/qml/tst_plotcore_parity.qml` held each
port to its original by asking both the same questions and comparing the
answers exactly: values bit for bit, labels character for character.

That suite can only run where the QML is, and the QML goes once H5Scope draws
through `Figure`. `answers.jsonl` keeps what it established:

- **What it holds:** every question the suite asked, with the answer it was
  given.
- **How it was recorded:** H5Scope's `tests/PlotCoreProbe.cpp` was patched (not
  committed) to append each call's arguments and plotcore's answer to a file,
  and the suite was run once:

  ```sh
  PLOTCORE_RECORD=parity.jsonl QT_QPA_PLATFORM=offscreen \
      build/release/bin/test_qml -input tests/qml/tst_plotcore_parity.qml
  ```

- **When:** at H5Scope `a6e03fc`, with plotcore pinned at `6340d9e`.
- **Why the answers are the viewer's too:** all 15 of the suite's cases passed
  on that run.

`test_plotviewer` asks each question again and holds the answer to the
recorded one: a label character for character, and a number within the
golden digest's relative 1e-9. The viewer's suite compared numbers bit for
bit, but it compared two answers worked out on one machine. These were
recorded with glibc's libm, and MSVC's `log` rounds a few of them an ulp or
five the other way. **A changed answer is a changed rule**, and the viewer's
rule at that: change the file when that is what you mean, and say so in the
commit.

## The format

One JSON object per line: `{"f": function, "a": [arguments], "r": answer}`,
in plotcore's terms rather than the probe's.

- **Doubles** are written as `%.17g` strings, so that NaN, the infinities and
  −0 survive JSON.
- **The colours table.** The first line, `"f": "colours"`, is the table of
  colour settings that `seriesColor` asks about. Each `seriesColor` line names
  its settings by their index in that table, because there are 56 of them
  across 6,496 questions.
- **No duplicates.** A question asked twice is written once.
- **`paperColor`.** Its one argument, a colour, was recorded as the argument
  list itself.
