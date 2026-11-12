<!-- SPDX-FileCopyrightText: 2026 Jonas Sattler -->
<!-- SPDX-FileCopyrightText: 2026 Andreas Martin -->
<!-- SPDX-License-Identifier: BSD-3-Clause -->

# jonassattler/main and a-ma72/main

A working note, not part of the library documentation. The tips compared here
are `upstream/main` (`732b81c`, jonassattler) and `origin/main` (`e7bf971`,
a-ma72). The GitHub history of jonassattler/plotcore has one commit after the
shared point. The working tree on `origin/main` is not committed: a wide
base-1 column takes its extremes from the pyramid instead of from the
samples.

## Shared history

Both branches contain the same history through `52b2981` (21 October 2026,
merge of `jonassattler/bsd-3-clause`). That is plotcore 0.2.1, BSD-3-Clause,
with the viewer's picture, the scene marks, and the demo licences. Neither
side has merged the other since.

## jonassattler/main

This branch stays the library the viewer asks every frame. The only commit
after the split, `732b81c` of 5 November, *Keep what a line spans instead of
walking it on every frame*, does not change the picture and does not change
`golden/expected.txt`.

A figure asks each line once a frame for its extent, its x span, and its
smallest positive x. Those were walks of the base or of the time axis:
measured 20.7 ms a frame on one line of ten million values around zero, and
45.8 ms against a time base. The three results stay on the line until
`forget()` drops them. The same pan then costs 0.13 ms and 0.34 ms.
`Plot::wholeLineWalks()` counts the walks, because a test holds behaviour to
a count and not to a duration.

## a-ma72/main

This branch makes the plot usable as a host, and accepts a different picture
to do it. Six commits, none of them on jonassattler/main:

- `1e8c208` — the PyQt6 window grew slow because every mouse move rebuilt the
  lines in Python point by point and rebuilt the line table on every drag.
  The ground, the grid, and the lines stay put and are only copied; a line is
  handed to Qt in one piece; a drag or a wheel updates only scale, shift, and
  the window. Only `python/src/plotcore/qt.py` and
  `python/pyqt6-demo/demo.py`.
- `453d130` — a line whose pyramid base is one sample is drawn with the same
  column procedure as plotlib: one column per device pixel, a vertical at the
  column's middle or the chord from edge to edge. `golden/expected.txt` is
  rewritten. That is a changed picture.
- `7220890` — mouse gestures: Shift is x, Ctrl is y, Alt moves and scales a
  line. Camera, Figure, the Qt widget, and `qt.py`.
- `8bf99dc` — column scratch is reused across the lines of a figure instead
  of being allocated on every `projectLine`. The time order Plot already
  knows is passed through.
- `41b8918` — rounding, and where a short line ends. Logarithmic column edges
  sit on pixel boundaries and fell on different pixels per libm; they are
  placed from the grid index. An edge past the last sample held that sample's
  value out to the right of the frame, so a short blue line (the square wave)
  ran across the whole axis and disappeared once a zoom left its samples
  behind. The line now ends at its last sample. Also in this commit: the Rust
  reader tolerance loosened to one pixel, retries for `apt-get` in
  `.github/workflows/ci.yml`, and `golden/expected.txt` again.
- `e7bf971` — a pan by whole columns keeps each line's column buffer and
  slides the columns that stay on screen, so only the strip that enters is
  scanned. The drag moves the window by whole device columns. The Qt widget,
  `python/src/plotcore/qt.py`, and matplotlib copy the lines already drawn
  and stroke that strip. The three span answers are kept on the line, the
  same fields as `732b81c`, and dropped by `forget()`. The commit message
  names the column pan. Neither the slide nor the span cache changes the
  picture, and `golden/expected.txt` is not in this commit.

`git diff upstream/main origin/main` is 26 files, 3267 lines added and 724
removed. Both tips keep `extent`, `timeSpan`, and `timePositive`. Upstream
does not have the base-1 column projection, the gestures, the column
scratch, the blit, or the golden changes. The working tree on `e7bf971`
answers a crowded base-1 column from the pyramid levels
(`extremesInside`). A column whose finite samples are one value, and a
logarithmic y, still read the samples. The picture is unchanged.

## Representation

Both refuse to drop a one-sample spike, and both keep y as the extreme that
was actually taken. They differ on where that extreme is drawn, and on what a
pan is allowed to change.

jonassattler/main draws the extreme at the element where it occurred.
Thinning is a power-of-two bucket aligned to the axis, not to the window. A
pan changes which buckets are on screen and nothing about what is inside
them, so the line translates. A zoom steps from one octave to the next: one
honest change of detail, not a shimmer. Sub-pixel x is kept. The only
deliberate lie in `place()` is the clamp at ten million pixels, so a huge
value does not become an infinity in a float vertex.

a-ma72/main draws one column per device pixel, anchored at zero (at one on a
log axis). A column taller than a pixel is the vertical at the column's
middle, not at the sample. Shallower columns are the chord from edge to edge.
`place()` then snaps x to the centre of the device pixel it fell in; y is not
snapped. The premise is that a screen cannot show a finer x than a pixel, and
a stroke that crawls inside a pixel while the window slides is a worse
reading than a stroke that moves by whole pixels. The cost is that the drawn
x is up to half a pixel off the sample, and a tall spike is drawn at the
middle of its column rather than at the element.

## Measurement cursor

The numbers and the stroke are not the same question, and only the fork
separates them.

On both branches `nearestSample` names a real sample: `reading.x` and
`reading.y` are that sample's own values, and the search is an index plus 64
samples either side, not a walk of the line. A gap wider than that is
declined, for the same reason the stroke breaks.

jonassattler/main searches the thinned envelope, whose points already sit at
their elements. The dot, the stroke, and the number agree. What you measure
when zoomed out is the extreme of the bucket, not every sample under the
pixel.

a-ma72/main with a base of one sample searches the samples themselves, so the
number is the sample under the pointer even when the stroke is the column
vertical. The dot is placed from the sample's true x, not from the snapped
stroke. The half-pixel tolerance is in the stroke and in `valueOf`, which
inverts the snapped pixel back into data for a host that draws those
coordinates (matplotlib). It is not in the readout text.

How much half a pixel is: one device pixel of the current window. Zoomed so
that a sample is many pixels wide, it is a fraction of a sample and not
visible. Zoomed out so that one pixel holds hundreds of samples, the stroke
is already a column-wide vertical; a finer x would not be a different pixel.
The number still names a sample. The tolerance matters for a digest or a host
that treats the inverted stroke as data, which is why the Rust reader test on
the fork allows one pixel of the window and not `1e-6`.

## Memory

Every frame still builds fresh `points`, `runs`, `values`, and `stroke`
vectors inside the compose loop, on both branches. Neither recycles those.

Both tips keep three small cached answers on the line (`extent`, `timeSpan`,
`timePositive`), dropped by `forget()`. That is a few words, not a buffer.
It removes a walk, not an allocation. The pyramid is the copy of the line,
as before.

a-ma72/main adds `ColumnScratch`: one vector of column mins and maxes and one
of edges, grown to the pane. On `41b8918` one scratch is reused across the
lines of a frame. On `e7bf971` each drawn line keeps its own
(`columnKept_`). `retain` is off unless the owner keeps the scratch for that
line: the shared thread-local one is what tests use, and they rewrite a
sample between projections. A summarised fold is not slid. Its columns are
rewritten into the same buffer as the window moves. A pan by whole columns
calls `shiftBy` and walks only the strip that entered (`scanned` is that
count). A pan that does not cross a column walks nothing. The Python widget
keeps one `QImage` of the lines, and the C++ widget the same. That image is
the hungry object: width times height times four bytes. A crosshair move
reuses it. A pan of whole device pixels scrolls it, which is the next
section. Matplotlib keeps the axes bitmap instead.

The working tree on `e7bf971` changes what that walk reads when the base is
one sample and a column holds many of them. `extremesInside` is
`extremesOver` without the seam past the column. Two different finite
extremes are already more than one sample, which is all a vertical asks, so
the column takes them from the levels and does not walk its samples. A full
view of 200 000 samples over 1 000 columns read 3 996 base elements, about
four a column. The chord at an edge still reads the two samples it falls
between. A column whose finite samples are one value still reads them,
because the pyramid does not store how many there were: one finite sample
is a chord, and reporting it as more than one draws the vertical. A
logarithmic y still reads them, because the levels do not separate a
positive sample from the rest. Both of those reads are the base vector in
RAM. The file was copied when the line was taken in. A window with at most
one sample a column reads those samples and does not ask the levels to
aggregate them. `samplesRead` on the scratch is the count a test holds.

## Scrolling and panning

A region that was already drawn must still be the same picture after the
window moves, so the host can copy it instead of folding it again. The copy
is a blit. The step of the column grid is what makes the copy honest.

One column is one device pixel, and its width in data is the window span
divided by that count. The edges are multiples of that width, anchored at
zero (at one on a log axis), so a pan of less than a column leaves every
sample in the column it was in. A pan of a whole number of columns shifts the
grid by that many columns: the columns that remain on screen hold the same
samples they held, and they land on the pixels the window moved them to.
Those pixels are the region a blit can scroll. Only the strip that has come
into view needs a new fold. A step that reshuffled samples inside a column
would make the copy a lie, which is why the width is that step and not a
window-aligned bucket.

On jonassattler/main a pan is the case the extent cache was measured for.
Nothing the extent depends on moves, so the frame does not walk a
ten-million-sample line (20.7 ms down to 0.13 ms around zero, 45.8 ms down to
0.34 ms with a time base). The envelope translates because its buckets are
aligned to the axis. The picture moves rigidly, which is its own reason a
host could scroll a bitmap. The branch does not do that scroll; it spends the
saving on not walking the line.

On a-ma72/main at `41b8918` the grid is the precondition for copying the
overlap, and it is in the library. The copy was not. `python/src/plotcore/qt.py`
blitted the stored pane only while the view matched, so a pan threw the image
away and stroked the whole body again. What the widget reused was a body that
did not move: the crosshair.

`e7bf971` does the scroll the grid allows. The drag only moves the window by
whole device columns. `Figure::move` holds the pointer's fraction and calls
`panBy` once a column has been crossed, on both axes. A column is one device
pixel of that axis, `llround(extent * pixelRatio)`, which is the column
`projectLine` draws. A shorter drag leaves the window where it was, so the
picture stays the one already drawn. N columns move the window by N times
that width, so every sample that stays on screen stays in its column and
lands N pixels over. A wheel still changes the column width, and a window
stated in values is not rounded onto the grid. In the working tree the
strip that enters, when the base is one sample and the column's extremes
differ, is folded from the pyramid rather than from the samples it holds.

`python/src/plotcore/qt.py` (`_scroll_lines`) and the C++ `PlotWidget`
(`scrollLines`) copy the lines image by whole device pixels and stroke the
strip that enters, plus a seam of a couple of pixels so the antialiased
edge is drawn again. The grid and the frame are painted every frame; the
gutters do not move, so only the lines are in the image. A shift that is
not a whole device pixel is declined. A shift of zero pixels puts the
stored image back when the body still matches. A nonzero shift is taken
only when the overlap is the old picture translated by that many pixels. A
flipped view is not scrolled.

`python/src/plotcore/mpl.py` copies the axes bitmap the same way, and it
copies a nonzero whole-pixel pan even when the new fold is not a rigid
translation of the points already drawn. Rebuilding the pane is what made a
square wave's gaps jump. When the points have not moved, the shift is taken
as zero and the same bitmap is put back. The saved region counts down from
the top of the canvas and the pan counts up, so the restore uses the
opposite of that vertical step. Adding them sent a downward drag upward.
Drawing the ticks paints into the top and bottom rows of the pane. Those
rows are the line, so the pane is put back afterwards and the ticks stay in
the margin. The foot under the pane is reserved for matplotlib's own tick,
its pad, and the label. The mark is centred on the rule, but the number is
seated a whole mark below it, and a foot that stops at the type lets the
rule and the restored pane paint out the tick and the top of the digits.
The Qt canvas is not erased before that bitmap is shown. Its `paintEvent`
called `eraseRect` first, and the white flash was the flicker on a pan.

`ColumnScratch` shifts with the same grid, so the columns that remain are
not scanned again, and a pan that stays inside a column scans none. The
extent cache is kept with it, so that pan also does not walk the line for
the three spans. A line that is not already in the pyramid still has to be
read; the cache only skips the walk of what is held.

A wheel zoom on jonassattler/main stays on the same octave of buckets until
the zoom crosses a power of two, then the detail changes once. A wheel zoom
on a-ma72/main changes the column width, so the grid is a different step and
no stored column is the same picture. The columns are filled again. In the
working tree a crowded one is filled from the pyramid. Nothing is copied.
That is the picture the fork chose: one column per pixel
of the window you now have, not the octave you were already in.

## On one side only

The extent cache is on both tips: `wholeLineWalks`, `extent`, `timeSpan`,
and `timePositive`. The fields are the same.

The fork only, on `e7bf971`: the base-1 column projection, the mouse
gestures (Shift, Ctrl, Alt), the per-line `columnKept_`, the drag that
steps by whole device columns, the line image and its scroll in the Qt
widget, in `qt.py`, and in matplotlib — the taller foot, and the canvas
that is not erased before the bitmap is shown — and the rewritten
`golden/expected.txt`.

The working tree only: `extremesInside`, and a crowded base-1 column filled
from it. Upstream folds a wide view from the pyramid too, as a power-of-two
envelope drawn at the element. The fork's column stroke is a different
picture, now read from the same levels.

## A merge

A later merge is not a fast-forward either way.

The extent cache is already on both tips and does not touch the picture.
The working tree's column query does not touch it either. It removes the
walk a wide base-1 view paid over samples the pyramid already held.

The fork's golden file cannot land on upstream without taking the fork's
picture with it: the column stroke, the pixel snap, and a short line that
ends at its last sample. The gestures and the blit are host behaviour beside
that picture. They do not by themselves require the golden file, but they
were written against the column grid, and the blit is honest only while that
grid keeps a drawn region the same picture.
