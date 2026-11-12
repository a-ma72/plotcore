# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-FileCopyrightText: 2026 Andreas Martin
# SPDX-License-Identifier: BSD-3-Clause

"""matplotlib draws the folds plotcore made, on its own axes, and its events
are the figure's gestures."""

import math

import numpy as np
import pytest

matplotlib = pytest.importorskip("matplotlib")
matplotlib.use("Agg")

from matplotlib.backend_bases import MouseEvent  # noqa: E402
from matplotlib.figure import Figure as MplFigure  # noqa: E402

import plotcore  # noqa: E402
from plotcore import mpl  # noqa: E402


def spiked(count=1_000_000, at=777_777):
    values = np.sin(np.arange(count) / 1000.0)
    values[at] = 50.0
    return values


def test_a_spike_one_sample_wide_survives_into_matplotlib():
    figure = plotcore.Figure()
    figure.add_line(spiked(), "spiked")
    ax = mpl.draw(figure, MplFigure().add_subplot(111), size=(640, 400))
    drawn = ax.get_lines()[0]
    assert np.nanmax(drawn.get_ydata()) == 50.0
    assert len(drawn.get_xdata()) < 10_000, "the fold, not the million samples"
    x0, x1, y0, y1 = figure.view
    assert ax.get_xlim() == pytest.approx((x0, x1))
    assert ax.get_ylim() == pytest.approx((y0, y1))


def test_matplotlib_takes_the_scales_the_figure_has():
    figure = plotcore.Figure()
    figure.add_line(10.0 ** np.linspace(-3, 3, 10_000), "decades")
    figure.set_index_axis(1.0, 1.0)
    figure.settings["xLog"] = True
    figure.settings["yLog"] = True
    figure.settings["yLogBase"] = 2.0
    figure.settings["title"] = "both logarithmic"
    ax = mpl.draw(figure, MplFigure().add_subplot(111), size=(640, 400))
    assert ax.get_xscale() == "log" and ax.get_yscale() == "log"
    assert ax.get_title() == "both logarithmic"


def test_an_axis_of_its_own_is_a_twin_with_that_window():
    figure = plotcore.Figure()
    figure.add_line(np.arange(1000.0), "common")
    figure.add_line(1e6 + np.arange(1000.0), "own")
    figure.line(1).settings["ownAxis"] = True
    figure.line(1).settings["axisLabel"] = "big"
    fig = MplFigure()
    ax = mpl.draw(figure, fig.add_subplot(111), size=(640, 400))
    twins = [a for a in fig.axes if a is not ax]
    assert len(twins) == 1
    low, high = twins[0].get_ylim()
    assert low < 1e6 < 1e6 + 999 < high
    assert twins[0].get_ylabel() == "big"
    assert np.nanmax(twins[0].get_lines()[0].get_ydata()) == pytest.approx(1e6 + 999)


def test_the_live_view_is_moved_by_the_figure_s_gestures():
    figure = plotcore.Figure()
    figure.add_line(spiked(100_000, 77_777), "spiked")
    fig = MplFigure(figsize=(8, 5), dpi=100)
    from matplotlib.backends.backend_agg import FigureCanvasAgg

    FigureCanvasAgg(fig)
    view = mpl.View(figure, fig)
    view.redraw()
    before = figure.view
    box = view.ax.get_window_extent()
    middle = ((box.x0 + box.x1) / 2, (box.y0 + box.y1) / 2)
    scroll = MouseEvent("scroll_event", fig.canvas, *middle, step=2)
    fig.canvas.callbacks.process("scroll_event", scroll)
    zoomed = figure.view
    assert zoomed[1] - zoomed[0] < before[1] - before[0]
    assert view.ax.get_xlim() == pytest.approx(zoomed[:2])
    for name, x, button in (("button_press_event", middle[0], 1),
                            ("motion_notify_event", middle[0] + 50, 1),
                            ("button_release_event", middle[0] + 50, 1)):
        fig.canvas.callbacks.process(name, MouseEvent(name, fig.canvas, x, middle[1],
                                                      button=button))
    assert figure.view[0] < zoomed[0], "a drag to the right shows what is to the left"


def _axes_rgba(view):
    buf = np.array(view.mpl.canvas.buffer_rgba())
    x0, y0, x1, y1 = (int(v) for v in view.ax.bbox.extents)
    # The buffer's first row is the top of the figure; display y grows up.
    height = buf.shape[0]
    return buf[height - y1:height - y0, x0:x1].copy()


def test_a_pan_copies_the_axes_and_a_fraction_of_a_column_does_not_redraw_them():
    figure = plotcore.Figure()
    figure.add_line(np.sin(np.arange(20_000) * 0.02), "wave")
    figure.settings["legend"] = False
    fig = MplFigure(figsize=(6, 4), dpi=100)
    from matplotlib.backends.backend_agg import FigureCanvasAgg

    FigureCanvasAgg(fig)
    view = mpl.View(figure, fig)
    view.redraw()
    area = view._scene.area
    x0, x1, y0, y1 = figure.view
    column = (x1 - x0) / area.width
    before = _axes_rgba(view)

    # Inside the column the snapped stroke has not moved, so the bitmap is
    # the one already drawn.
    held = x0
    for fraction in (0.05, -0.05, 0.15, -0.15, 0.4, -0.4):
        candidate = x0 + fraction * column
        if math.floor(-candidate / column) == math.floor(-x0 / column):
            held = candidate
            break
    assert held != x0, "a fraction of a column stays in the same pixel"
    figure.set_view(held, held + (x1 - x0), y0, y1)
    view.redraw()
    assert view.held == 1
    assert view.scrolls == 0
    assert np.array_equal(_axes_rgba(view), before)

    # One column further than the picture that was copied is one pixel.
    jump = x0 + column
    figure.set_view(jump, jump + (x1 - x0), y0, y1)
    view.redraw()
    assert view.scrolls == 1
    after = _axes_rgba(view)
    # The window moved right, so a pixel that stayed is the one that was one
    # to its right. The edge is stroked again and is not part of the copy.
    inset = 8
    assert np.array_equal(after[inset:-inset, inset:-inset],
                          before[inset:-inset, inset + 1:-(inset - 1)])

    scrolls = view.scrolls
    zx0, zx1, zy0, zy1 = figure.view
    figure.set_view(zx0, zx0 + (zx1 - zx0) / 2, zy0, zy1)
    view.redraw()
    assert view.scrolls == scrolls, "a zoom is a different picture"


def _flank_gaps(image: np.ndarray) -> np.ndarray:
    """Distances between the vertical edges of a square wave, in pixels."""
    row = image[image.shape[0] // 2, :, 0].astype(float)
    edges = np.where(np.abs(np.diff(row)) > 40)[0]
    gaps = np.diff(edges)
    return gaps[gaps > 2]


def test_panning_a_square_wave_keeps_the_gaps_it_already_drew():
    figure = plotcore.Figure()
    figure.add_line(np.where((np.arange(20_000) // 250) % 2 == 0, -1.0, 1.0), "square")
    figure.settings["legend"] = False
    figure.settings["grid"] = "none"
    fig = MplFigure(figsize=(8, 5), dpi=100)
    from matplotlib.backends.backend_agg import FigureCanvasAgg

    FigureCanvasAgg(fig)
    view = mpl.View(figure, fig)
    view.redraw()
    before = _axes_rgba(view)
    gaps = _flank_gaps(before)
    assert len(gaps) > 4
    box = view.ax.get_window_extent()
    x, y = (box.x0 + box.x1) / 2, (box.y0 + box.y1) / 2
    fig.canvas.callbacks.process("button_press_event",
                                MouseEvent("button_press_event", fig.canvas, x, y, button=1))
    for step in range(1, 9):
        fig.canvas.callbacks.process(
            "motion_notify_event",
            MouseEvent("motion_notify_event", fig.canvas, x - step, y, button=1))
    after = _axes_rgba(view)
    assert view.scrolls == 8, "each pixel of the drag copies the pane"
    assert np.median(_flank_gaps(after)) == np.median(gaps)
    # Eight pixels to the left. A wiped top or bottom row would disagree
    # across the pane; the cursor is a few pixels and travels with the pointer.
    shift = 8
    for row in (0, -1):
        copied = after[row, :-(shift + 3)]
        source = before[row, shift:-3]
        assert np.any(copied != source, axis=1).sum() < 30


def test_a_downward_drag_moves_the_copied_pane_downward():
    figure = plotcore.Figure()
    figure.add_line(np.sin(np.arange(4_000) * 0.02), "wave")
    figure.settings["legend"] = False
    figure.settings["grid"] = "none"
    fig = MplFigure(figsize=(6, 4), dpi=100)
    from matplotlib.backends.backend_agg import FigureCanvasAgg

    FigureCanvasAgg(fig)
    view = mpl.View(figure, fig)
    view.redraw()
    before = _axes_rgba(view)
    box = view.ax.get_window_extent()
    x, y = (box.x0 + box.x1) / 2, (box.y0 + box.y1) / 2
    fig.canvas.callbacks.process("button_press_event",
                                MouseEvent("button_press_event", fig.canvas, x, y, button=1))
    shift = 6
    # Display y grows up, so a smaller y is the mouse moving down.
    fig.canvas.callbacks.process(
        "motion_notify_event",
        MouseEvent("motion_notify_event", fig.canvas, x, y - shift, button=1))
    after = _axes_rgba(view)
    assert view.scrolls == 1
    # Row 0 is the top of the pane. The picture follows the pointer, so the
    # row that was at i is now at i + shift. The strip that entered is the
    # top, and the cursor is a few pixels wide.
    moved = after[8 + shift:-8, 8:-8]
    source = before[8:-8 - shift, 8:-8]
    assert np.mean(np.any(moved != source, axis=2)) < 0.02


def test_the_x_tick_labels_stay_on_the_canvas():
    figure = plotcore.Figure()
    figure.add_line(np.sin(np.arange(2_000) * 0.02), "wave")
    figure.settings["legend"] = False
    fig = MplFigure(figsize=(8, 5), dpi=100)
    from matplotlib.backends.backend_agg import FigureCanvasAgg

    FigureCanvasAgg(fig)
    view = mpl.View(figure, fig)
    view.redraw()
    renderer = fig.canvas.get_renderer()
    labels = [label for label in view.ax.get_xticklabels() if label.get_text()]
    assert labels
    for label in labels:
        assert label.get_window_extent(renderer).y0 >= 0.0


def test_the_x_ticks_stay_below_the_rule_and_off_the_digits():
    figure = plotcore.Figure()
    figure.add_line(np.sin(np.arange(2_000) * 0.02), "wave")
    figure.settings["legend"] = False
    fig = MplFigure(figsize=(8, 5), dpi=144)
    from matplotlib.backends.backend_agg import FigureCanvasAgg

    FigureCanvasAgg(fig)
    view = mpl.View(figure, fig)
    view.redraw()
    renderer = fig.canvas.get_renderer()
    spine = view.ax.bbox.y0
    label, tick = next((label, tick) for label, tick in zip(
        view.ax.get_xticklabels(), view.ax.xaxis.get_major_ticks()) if label.get_text())
    digits = label.get_window_extent(renderer)
    mark = tick.tick1line.get_window_extent(renderer)
    assert digits.y0 >= 0.0
    # The top of the digits stays below the rule, and below the tick.
    assert digits.y1 < spine - 1.0
    assert mark.y0 < spine - 1.0
    assert digits.y1 <= mark.y0 + 0.5


def test_presenting_the_buffer_does_not_erase_the_widget_first():
    erased = []

    class Painter:
        def eraseRect(self, rect):
            erased.append(rect)

    class Canvas:
        def paintEvent(self, event):
            Painter().eraseRect(event)
            erased.append("drew")

    mpl._present_without_clearing(Canvas, Painter)
    Canvas().paintEvent("widget")
    assert erased == ["drew"]
    # The real erase is back once the buffer has been shown.
    Painter().eraseRect("after")
    assert erased == ["drew", "after"]
    # A second view does not wrap the method again.
    hooked = Canvas.paintEvent
    mpl._present_without_clearing(Canvas, Painter)
    assert Canvas.paintEvent is hooked
