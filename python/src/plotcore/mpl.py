# SPDX-FileCopyrightText: 2026 Andreas Martin
# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-License-Identifier: BSD-3-Clause

"""A figure drawn by matplotlib.

The axes are matplotlib's: its ticks, its logarithm, its fonts, with the limits
taken from the figure's view. The lines are not -- they are the folds plotcore
reduced each line to, handed over in the data's own units, so a spike one
sample wide is still there and a gap is still a gap. A line on an axis of its
own is drawn on a twin axis with that axis's window; one that is only moved or
scaled is drawn on a twin axis with no spine, for the reason the figure gives
it none.

The frame is matplotlib's, which is this host's one decision; what answers the
reader -- the crosshair and its reading, the band, why the pane cannot draw --
is the scene's overlay, painted as the scene lays it out.

`draw()` paints once, for a picture. `View` keeps a matplotlib figure live: a
scroll, a drag and a band go through the plotcore figure's own gestures, so
matplotlib zooms the line rather than the pane of it that was last drawn. A
pan copies that pane. The stroke only moves by whole pixels, so a fractional
move puts the same bitmap back and the strip that crosses a column is the
only part drawn again. The window is not wiped before that bitmap is shown:
Qt's canvas erased it first, and the white flash was the flicker on a pan.

    pip install plotcore[matplotlib]
"""

from __future__ import annotations

import math
from typing import List, Optional, Sequence, Tuple

import numpy as np

try:
    from matplotlib.lines import Line2D
    from matplotlib.patches import Rectangle
    from matplotlib.ticker import MultipleLocator
    from matplotlib.transforms import Bbox, IdentityTransform
except ImportError as error:  # pragma: no cover
    raise ImportError("plotcore.mpl needs matplotlib: pip install plotcore[matplotlib]") from error

from .figure import Button, Figure, Modifiers
from .scene import FORM_RUNS, FORM_VALUES, Align, Layer, MarkKind, Scene, TextRole

# How far apart the spines of axes of their own sit, in points.
_SPINE = 48
# The join is stroked again over the copied picture, so the antialiased edge
# of the old stroke does not sit beside a freshly stroked one.
_SEAM = 2

_CORNERS = {
    "top-left": "upper left",
    "top-right": "upper right",
    "bottom-left": "lower left",
    "bottom-right": "lower right",
}


def _joined(runs: List[np.ndarray]) -> Tuple[np.ndarray, np.ndarray]:
    """Every run of a line in one array, with a NaN between two runs so
    matplotlib does not stitch across a gap."""
    if not runs:
        return np.zeros(0), np.zeros(0)
    parts = []
    for i, run in enumerate(runs):
        if i:
            parts.append(np.array([[np.nan, np.nan]]))
        parts.append(run)
    joined = np.concatenate(parts)
    return joined[:, 0], joined[:, 1]


def _scale(axis_setter, logarithmic: bool, base: float, low: float, high: float) -> None:
    if logarithmic and low > 0.0 and high > low:
        axis_setter("log", base=base)
    else:
        axis_setter("linear")


def _paint(figure: Figure, scene: Scene, ax, extra: list, *, top: bool = True) -> list:
    """Draw `scene` onto `ax`, adding any twin axes made to `extra`.

    Returns the line artists in line order. The legend and the overlay are
    painted when `top` is set; a live view leaves them off the bitmap it
    copies, and paints them again after the copy."""
    settings = figure.settings
    flipped = settings["flipped"]
    chrome = scene.chrome
    x0, x1, y0, y1 = scene.view
    x_log, y_log = settings["xLog"], settings["yLog"]
    x_base, y_base = settings["xLogBase"], settings["yLogBase"]

    ax.set_facecolor(chrome.ground[:3])
    ax.figure.set_facecolor(chrome.ground[:3])
    for spine in ax.spines.values():
        spine.set_color(chrome.axis_rule[:3])
    ax.tick_params(colors=chrome.ink[:3], which="both")

    # The common axes: x along the foot, or up the side when flipped.
    along, across = (ax.set_ylim, ax.set_xlim) if flipped else (ax.set_xlim, ax.set_ylim)
    along_scale, across_scale = ((ax.set_yscale, ax.set_xscale) if flipped
                                 else (ax.set_xscale, ax.set_yscale))
    along_scale("linear")
    across_scale("linear")
    along(x0, x1)
    across(y0, y1)
    _scale(along_scale, x_log, x_base, x0, x1)
    common = any(not line.own for line in scene.lines) or not scene.lines
    if common:
        _scale(across_scale, y_log, y_base, y0, y1)
    (ax.xaxis if flipped else ax.yaxis).set_visible(common)

    grid = settings["grid"]
    ax.grid(False)
    if grid != "none":
        rule = dict(color=chrome.rule_major[:3], linewidth=0.8)
        ax.grid(True, which="both" if grid == "dense" else "major", **rule)
        if grid == "dense":
            ax.minorticks_on()
            ax.grid(True, which="minor", color=chrome.rule_minor[:3], linewidth=0.5)
        if grid == "custom":
            for step, axis in ((settings["gridStepX"], ax.yaxis if flipped else ax.xaxis),
                               (settings["gridStepY"], ax.xaxis if flipped else ax.yaxis)):
                if step > 0 and axis.get_scale() == "linear":
                    axis.set_major_locator(MultipleLocator(step))

    handles = []
    artists = []
    slot = 0
    markers = settings["markers"]
    names = {entry.line: entry.name for entry in scene.legend}
    for line in scene.lines:
        xs, ys = _joined(line.values)
        if flipped:
            xs, ys = ys, xs
        if line.own:
            twin = ax.twiny() if flipped else ax.twinx()
            extra.append(twin)
            limits = twin.set_xlim if flipped else twin.set_ylim
            setter = twin.set_xscale if flipped else twin.set_yscale
            setter("linear")
            limits(line.y_low, line.y_high)
            _scale(setter, line.y_log, y_base, line.y_low, line.y_high)
            side = "top" if flipped else "right"
            spine = twin.spines[side]
            if line.numbered:
                spine.set_position(("outward", _SPINE * slot))
                spine.set_color(line.colour[:3])
                twin.tick_params(axis="x" if flipped else "y", colors=line.colour[:3])
                label = figure.line(line.line).settings["axisLabel"]
                if label:
                    (twin.set_xlabel if flipped else twin.set_ylabel)(label,
                                                                      color=line.colour[:3])
                slot += 1
            else:
                spine.set_visible(False)
                (twin.xaxis if flipped else twin.yaxis).set_visible(False)
            target = twin
        else:
            target = ax
        drawn, = target.plot(
            xs, ys, color=line.colour[:3], alpha=line.colour[3], linewidth=line.width,
            solid_capstyle="butt",
            marker="o" if markers and len(line.markers) else None,
            markersize=1.5 * line.marker_radius,
        )
        artists.append(drawn)
        if line.line in names:
            drawn.set_label(names[line.line])
            handles.append(drawn)

    if settings["title"]:
        ax.set_title(settings["title"], color=chrome.ink[:3])
    x_label, y_label = settings["xLabel"], settings["yLabel"]
    (ax.set_ylabel if flipped else ax.set_xlabel)(x_label, color=chrome.ink[:3])
    if common:
        (ax.set_xlabel if flipped else ax.set_ylabel)(y_label, color=chrome.ink[:3])
    if top and handles and settings["legend"]:
        legend = ax.legend(handles=handles, loc=_CORNERS.get(settings["legendCorner"], "best"),
                           facecolor=chrome.ground[:3], edgecolor=chrome.axis_rule[:3],
                           labelcolor=chrome.ink[:3])
        legend.set_zorder(10)
    if top:
        _overlay(figure, scene, ax)
    return artists


_ROLES = {
    TextRole.TICK: "tickPixels",
    TextRole.TICK_MINOR: "tickMinorPixels",
    TextRole.TITLE: "titlePixels",
    TextRole.AXIS_NAME: "axisNamePixels",
    TextRole.LEGEND: "legendPixels",
    TextRole.NOTE: "notePixels",
}
_ACROSS = {Align.START: "left", Align.MIDDLE: "center", Align.END: "right"}
_DOWN = {Align.START: "top", Align.MIDDLE: "center", Align.END: "bottom"}


def _overlay(figure: Figure, scene: Scene, ax) -> list:
    """Paint the scene's overlay onto `ax`, whose box is the scene's pane: a
    pane pixel is a share of the axes, and a size in pixels is one in points
    at the figure's dpi. The artists are returned so a later frame can take
    them off before it copies the pane they were painted over."""
    area = scene.area
    if area.width <= 0 or area.height <= 0:
        return
    style = figure.style
    points = 72.0 / ax.figure.dpi

    def at(x: float, y: float) -> Tuple[float, float]:
        return (x - area.x) / area.width, 1.0 - (y - area.y) / area.height

    made = []
    for z, mark in enumerate(m for m in scene.over if m.layer == Layer.OVERLAY):
        order = 20 + z * 1e-3
        if mark.kind == MarkKind.SEGMENT:
            (ax0, ay0), (ax1, ay1) = at(mark.x0, mark.y0), at(mark.x1, mark.y1)
            drawn, = ax.plot([ax0, ax1], [ay0, ay1], transform=ax.transAxes,
                             color=mark.stroke[:3], alpha=mark.stroke[3],
                             linewidth=mark.width * points, clip_on=mark.clipped, zorder=order)
            made.append(drawn)
        elif mark.kind == MarkKind.BOX:
            # Edged inside its bounds: the edge's line is half its width in.
            half = mark.width / 2.0
            (left, top), (right, bottom) = (at(mark.x0 + half, mark.y0 + half),
                                            at(mark.x1 - half, mark.y1 - half))
            made.append(ax.add_patch(Rectangle(
                (left, bottom), right - left, top - bottom, transform=ax.transAxes,
                facecolor=mark.fill if mark.fill[3] > 0 else "none",
                edgecolor=mark.stroke if mark.width > 0 else "none",
                linewidth=mark.width * points, linestyle="--" if mark.dashed else "-",
                clip_on=mark.clipped, zorder=order)))
        elif mark.kind == MarkKind.CIRCLE:
            x, y = at(mark.x0, mark.y0)
            drawn, = ax.plot([x], [y], "o", transform=ax.transAxes,
                             markerfacecolor=mark.fill if mark.fill[3] > 0 else "none",
                             markeredgecolor=mark.stroke, markeredgewidth=mark.width * points,
                             markersize=2.0 * mark.radius * points, clip_on=mark.clipped,
                             zorder=order)
            made.append(drawn)
        elif mark.kind == MarkKind.TEXT:
            x, y = at(mark.x0, mark.y0)
            made.append(ax.text(
                x, y, mark.text, transform=ax.transAxes, color=mark.colour[:3],
                alpha=mark.colour[3], rotation=-mark.turn, rotation_mode="anchor",
                ha=_ACROSS[mark.across], va=_DOWN[mark.down],
                fontsize=style[_ROLES[mark.role]] * points, clip_on=mark.clipped,
                zorder=order))
    return made


def _pixels_of(ax) -> Tuple[int, int]:
    """The axes' own box, in display pixels."""
    box = ax.get_window_extent()
    return max(1, int(round(box.width))), max(1, int(round(box.height)))


def _axis_shift(old0: float, old1: float, new0: float, new1: float, pixels: int,
                logarithmic: bool) -> Optional[int]:
    """How many device pixels the snapped stroke moved on one axis.

    `place` pins an x to the centre of the device pixel it falls in, and a
    column edge keeps that pixel until the window crosses a grid line. The
    picture therefore stays, or it jumps by a whole pixel; it does not crawl.
    A zoom (the span changed) is not a jump of that picture."""
    if pixels <= 0:
        return None

    def coord(value: float) -> Optional[float]:
        if not logarithmic:
            return value
        if value <= 0.0:
            return None
        return math.log(value)

    a0, a1, b0, b1 = coord(old0), coord(old1), coord(new0), coord(new1)
    if None in (a0, a1, b0, b1):
        return None
    old_span = a1 - a0
    new_span = b1 - b0
    if not old_span > 0.0 or not new_span > 0.0:
        return None
    if abs(new_span - old_span) > 1e-6 * max(1.0, abs(old_span)):
        return None
    width = old_span / pixels

    def snapped(value: float) -> int:
        # A value a couple of ulps past an integer is still that integer.
        # floor would otherwise step an extra pixel and the copy would miss.
        quot = value / width
        nearest = round(quot)
        if abs(quot - nearest) <= 1e-8:
            quot = nearest
        return math.floor(-quot)

    return snapped(b0) - snapped(a0)


def _gathered(values: Sequence[np.ndarray]) -> np.ndarray:
    parts = [run[:, :2] for run in values if len(run)]
    if not parts:
        return np.zeros((0, 2))
    return np.concatenate(parts)


def _display(points: np.ndarray, x0: float, x1: float, y0: float, y1: float, width: int,
             height: int) -> np.ndarray:
    """Data points as pixels of the pane, origin at the lower left."""
    if len(points) == 0 or not (x1 > x0 and y1 > y0):
        return np.zeros((0, 2))
    points = points[np.isfinite(points).all(axis=1)]
    if len(points) == 0:
        return np.zeros((0, 2))
    return np.column_stack([(points[:, 0] - x0) / (x1 - x0) * width,
                            (points[:, 1] - y0) / (y1 - y0) * height])


def _crop(points: np.ndarray, width: int, height: int, inset_x: float, inset_y: float) -> np.ndarray:
    if len(points) == 0:
        return points
    keep = ((points[:, 0] >= inset_x) & (points[:, 0] <= width - inset_x) &
            (points[:, 1] >= inset_y) & (points[:, 1] <= height - inset_y))
    return points[keep]


def draw(figure: Figure, ax=None, *, size: Optional[Tuple[int, int]] = None):
    """Draw `figure` once onto the matplotlib axes `ax` (a new figure's when
    None) and return the axes. The lines are folded for `size` pixels, or for
    the axes' own size."""
    if ax is None:
        import matplotlib.pyplot as plt

        _, ax = plt.subplots()
    width, height = size if size is not None else _pixels_of(ax)
    figure.set_size(width, height)
    scene = figure.scene(forms=FORM_RUNS | FORM_VALUES)
    _paint(figure, scene, ax, [])
    return ax


def _without_clear(paint, painter) -> None:
    """Run `paint` while `painter.eraseRect` does nothing, then put it back."""
    erase = painter.eraseRect

    def keep(*_args, **_kwargs):
        return None

    painter.eraseRect = keep
    try:
        paint()
    finally:
        painter.eraseRect = erase


def _present_without_clearing(canvas_cls, painter) -> None:
    """Show the buffer without erasing the widget first.

    Qt calls the class's ``paintEvent``, so an instance cannot decline the
    erase on its own. The method copies a buffer that already covers the
    rectangle, and wiping the widget white before that copy is the flicker
    on a pan."""
    if getattr(canvas_cls, "_plotcore_keeps_widget", False):
        return
    original = canvas_cls.paintEvent

    def paintEvent(self, event):
        _without_clear(lambda: original(self, event), painter)

    canvas_cls.paintEvent = paintEvent
    canvas_cls._plotcore_keeps_widget = True


def _keep_widget(canvas) -> None:
    """The Qt canvas erases, then copies. Agg has no widget to erase."""
    if not any(cls.__name__ == "FigureCanvasQTAgg" for cls in type(canvas).__mro__):
        return
    from matplotlib.backends.backend_qtagg import FigureCanvasQTAgg
    from matplotlib.backends.qt_compat import QtGui

    _present_without_clearing(FigureCanvasQTAgg, QtGui.QPainter)


class View:
    """A live matplotlib figure over a plotcore figure.

    Matplotlib's own navigation is off: a scroll, a drag and a band are the
    plotcore figure's gestures, mapped from the axes' box onto its pane."""

    def __init__(self, figure: Figure, mpl_figure=None):
        if mpl_figure is None:
            import matplotlib.pyplot as plt

            mpl_figure = plt.figure()
        self.figure = figure
        self.mpl = mpl_figure
        self.ax = mpl_figure.add_subplot(111)
        self.ax.set_navigate(False)
        self._extra: list = []
        self._scene: Optional[Scene] = None
        self._artists: list = []
        self._overlay_artists: list = []
        self._legend = None
        # The bitmap the last whole paint left in the axes, and the window it
        # was painted for. A pan copies it; it is dropped when something else
        # draws the canvas, because that draw is not the copy.
        self._bg = None
        self._anchor: Optional[Tuple[float, float, float, float]] = None
        self._anchor_scene: Optional[Scene] = None
        self._capturing = False
        self.scrolls = 0
        self.held = 0
        canvas = mpl_figure.canvas
        _keep_widget(canvas)
        canvas.mpl_connect("draw_event", self._on_draw)
        canvas.mpl_connect("scroll_event", self._scroll)
        canvas.mpl_connect("button_press_event", self._press)
        canvas.mpl_connect("motion_notify_event", self._motion)
        canvas.mpl_connect("button_release_event", self._release)
        canvas.mpl_connect("axes_leave_event", self._leave)
        canvas.mpl_connect("resize_event", lambda _event: self.redraw())

    def _on_draw(self, _event) -> None:
        if not self._capturing:
            self._bg = None

    # --- drawing ------------------------------------------------------------

    def redraw(self) -> None:
        canvas_w, canvas_h = self.mpl.canvas.get_width_height()
        # The figure is the canvas. Its pane is what matplotlib's axes must
        # be, so a column of the fold is one pixel of the bitmap copied below.
        self.figure.set_size(canvas_w, canvas_h)
        self._reserve_foot()
        scene = self.figure.scene(forms=FORM_RUNS | FORM_VALUES)
        self._place(scene)
        width, height = max(1, int(round(scene.area.width))), max(1, int(round(scene.area.height)))
        if not self._copy(scene, width, height):
            self._repaint(scene)
        self._scene = scene

    def _place(self, scene: Scene) -> None:
        """Put the axes on the figure's pane, y down in the scene and up here."""
        canvas_w, canvas_h = self.mpl.canvas.get_width_height()
        area = scene.area
        if canvas_w <= 0 or canvas_h <= 0 or area.width <= 0 or area.height <= 0:
            return
        position = [area.x / canvas_w, (canvas_h - area.y - area.height) / canvas_h,
                    area.width / canvas_w, area.height / canvas_h]
        self.ax.set_position(position)
        for twin in self._extra:
            twin.set_position(position)

    def _copy(self, scene: Scene, width: int, height: int) -> bool:
        """Copy the axes bitmap across a pan. False when the picture is not
        that bitmap slid by whole pixels, and has to be painted again."""
        anchor = self._anchor
        held = self._anchor_scene
        if self._bg is None or anchor is None or held is None:
            return False
        if self.figure.settings["flipped"]:
            return False
        # The saved bitmap is in buffer rows, which count down from the top of
        # the canvas; the axes box counts up. Same rectangle, either way.
        x0, y0, x1, y1 = self._bg.get_extents()
        canvas_h = self.mpl.canvas.get_width_height()[1]
        box = self.ax.bbox
        same = (int(round(x1 - x0)) == width and int(round(y1 - y0)) == height
                and int(round(x0)) == int(round(box.x0))
                and int(round(y0)) == int(round(canvas_h - box.y1)))
        if not same:
            return False
        if (round(held.area.width) != width or round(held.area.height) != height
                or self._frame_key(held) != self._frame_key(scene)):
            return False
        dx = _axis_shift(anchor[0], anchor[1], scene.view[0], scene.view[1], width,
                         bool(self.figure.settings["xLog"]))
        dy = _axis_shift(anchor[2], anchor[3], scene.view[2], scene.view[3], height,
                         bool(self.figure.settings["yLog"]))
        if dx is None or dy is None or max(abs(dx), abs(dy)) >= min(width, height) - _SEAM:
            return False
        if any(line.own for line in scene.lines) and dy != 0:
            return False
        # The snap can step a pixel before the drawn points do. If the points
        # are still where the bitmap put them, the picture has not moved.
        # A pan whose fold is not a rigid translation is still copied: the
        # pane already drawn stays, and only the strip that enters is stroked.
        # Rebuilding it is what makes a square wave's plateaus change width.
        if self._same_overlap(held, scene, dx, dy, width, height):
            pass
        elif self._same_overlap(held, scene, 0, 0, width, height):
            dx, dy = 0, 0
        elif dx == 0 and dy == 0:
            return False
        self._capturing = True
        try:
            self._slide(scene, dx, dy, width, height)
        finally:
            self._capturing = False
        if dx == 0 and dy == 0:
            self.held += 1
        else:
            self.scrolls += 1
        return True

    def _frame_key(self, scene: Scene):
        settings = self.figure.settings
        chrome = scene.chrome
        return (settings["grid"], settings["gridStepX"], settings["gridStepY"],
                settings["markers"], settings["legend"], settings["title"],
                settings["xLog"], settings["yLog"], settings["flipped"],
                tuple(chrome.ground), tuple(chrome.rule_major), tuple(chrome.rule_minor),
                tuple((line.colour, line.width, line.own, line.numbered) for line in scene.lines))

    def _same_overlap(self, old: Scene, new: Scene, dx: int, dy: int, width: int,
                      height: int) -> bool:
        """The lines that stayed are the same pixels, slid by `(dx, dy)`."""
        if len(old.lines) != len(new.lines):
            return False
        inset_x = abs(dx) + _SEAM + 0.5
        inset_y = abs(dy) + _SEAM + 0.5
        for before, after in zip(old.lines, new.lines):
            if before.own and (before.y_low != after.y_low or before.y_high != after.y_high):
                return False
            yb = (before.y_low, before.y_high) if before.own else (old.view[2], old.view[3])
            ya = (after.y_low, after.y_high) if after.own else (new.view[2], new.view[3])
            left = _display(_gathered(before.values), old.view[0], old.view[1], yb[0], yb[1],
                            width, height)
            right = _display(_gathered(after.values), new.view[0], new.view[1], ya[0], ya[1],
                             width, height)
            left = left + np.array([dx, dy])
            left = _crop(left, width, height, inset_x, inset_y)
            right = _crop(right, width, height, inset_x, inset_y)
            if left.shape != right.shape or len(left) == 0:
                return False
            left = left[np.lexsort((left[:, 1], left[:, 0]))]
            right = right[np.lexsort((right[:, 1], right[:, 0]))]
            if not np.allclose(left, right, rtol=0.0, atol=0.05):
                return False
        return True

    def _slide(self, scene: Scene, dx: int, dy: int, width: int, height: int) -> None:
        canvas = self.mpl.canvas
        renderer = canvas.get_renderer()
        self._clear_top()
        if dx != 0 or dy != 0:
            shown = self._shown_window(dx, dy, width, height)
            self.ax.set_xlim(shown[0], shown[1], emit=False)
            self.ax.set_ylim(shown[2], shown[3], emit=False)
            for artist, line in zip(self._artists, scene.lines):
                xs, ys = _joined(line.values)
                artist.set_data(xs, ys)
            self._restore_shifted(renderer, dx, dy)
            self._stroke_strip(renderer, dx, dy)
            self._bg = canvas.copy_from_bbox(self.ax.bbox)
            self._anchor = shown
            self._anchor_scene = scene
            # Ticks are outside the pane. Drawing them also paints into the
            # top and bottom rows of it, and those rows are the line. Put the
            # pane back afterwards; the ticks stay in the margin.
            self._redraw_margins(renderer)
            renderer.restore_region(self._bg)
        else:
            renderer.restore_region(self._bg)
        self._paint_top(scene, renderer)
        canvas.blit(self.mpl.bbox)

    def _shown_window(self, dx: int, dy: int, width: int, height: int):
        """The window the copied bitmap now shows: the anchor, slid by the
        whole pixels the picture moved, so the ticks land on the copied grid."""
        x0, x1, y0, y1 = self._anchor
        return (x0 - dx * (x1 - x0) / width, x1 - dx * (x1 - x0) / width,
                y0 - dy * (y1 - y0) / height, y1 - dy * (y1 - y0) / height)

    def _restore_shifted(self, renderer, dx: int, dy: int) -> None:
        x0, y0, x1, y1 = (int(v) for v in self._bg.get_extents())
        # The saved region counts down from the top of the canvas. dy counts
        # up: positive means the picture moved up. Adding that number to the
        # region sends a downward drag upward, and the strip then meets a gap
        # instead of the line.
        buf_dy = -dy
        src_x0 = x0 + max(-dx, 0)
        src_x1 = x1 - max(dx, 0)
        src_y0 = y0 + max(-buf_dy, 0)
        src_y1 = y1 - max(buf_dy, 0)
        renderer.restore_region(self._bg, bbox=(src_x0, src_y0, src_x1, src_y1),
                                xy=(x0 + dx, y0 + buf_dy))

    def _strips(self, dx: int, dy: int) -> List[Bbox]:
        # Display coordinates, y up: the same rectangle the bitmap was copied
        # in, which the region stores counting down from the top of the canvas.
        x0, y0, x1, y1 = self.ax.bbox.extents
        boxes = []
        left, right = x0, x1
        # The vertical strip takes the corner, so the horizontal one does not
        # stroke it a second time.
        if dx > 0:
            edge = min(x1, x0 + dx + _SEAM)
            boxes.append(Bbox.from_extents(x0, y0, edge, y1))
            left = edge
        elif dx < 0:
            edge = max(x0, x1 + dx - _SEAM)
            boxes.append(Bbox.from_extents(edge, y0, x1, y1))
            right = edge
        if dy > 0 and right > left:
            boxes.append(Bbox.from_extents(left, y0, right, min(y1, y0 + dy + _SEAM)))
        elif dy < 0 and right > left:
            boxes.append(Bbox.from_extents(left, max(y0, y1 + dy - _SEAM), right, y1))
        return boxes

    def _stroke_strip(self, renderer, dx: int, dy: int) -> None:
        strips = self._strips(dx, dy)
        if not strips:
            return
        ground = self.ax.get_facecolor()
        for box in strips:
            Rectangle((box.x0, box.y0), box.width, box.height, transform=IdentityTransform(),
                      facecolor=ground, edgecolor="none", antialiased=False,
                      clip_on=False).draw(renderer)
        for box in strips:
            self._grid_in(renderer, box)
            for artist in self._artists:
                artist.set_clip_box(box)
                artist.set_clip_on(True)
                artist.draw(renderer)
                artist.set_clip_on(False)
                artist.set_clip_box(None)

    def _grid_in(self, renderer, box: Bbox) -> None:
        settings = self.figure.settings
        grid = settings["grid"]
        scene = self._anchor_scene
        if grid == "none" or scene is None:
            return
        chrome = scene.chrome
        x0, x1 = self.ax.get_xlim()
        y0, y1 = self.ax.get_ylim()
        specs = [("major", chrome.rule_major, 0.8)]
        if grid == "dense":
            specs.append(("minor", chrome.rule_minor, 0.5))
        for which, colour, width in specs:
            x_locator = (self.ax.xaxis.get_major_locator() if which == "major"
                         else self.ax.xaxis.get_minor_locator())
            y_locator = (self.ax.yaxis.get_major_locator() if which == "major"
                         else self.ax.yaxis.get_minor_locator())
            for x in np.atleast_1d(x_locator.tick_values(min(x0, x1), max(x0, x1))):
                line = Line2D([x, x], [y0, y1], transform=self.ax.transData, color=colour[:3],
                              alpha=colour[3], linewidth=width, solid_capstyle="butt",
                              clip_on=True)
                line.set_clip_box(box)
                line.draw(renderer)
            for y in np.atleast_1d(y_locator.tick_values(min(y0, y1), max(y0, y1))):
                line = Line2D([x0, x1], [y, y], transform=self.ax.transData, color=colour[:3],
                              alpha=colour[3], linewidth=width, solid_capstyle="butt",
                              clip_on=True)
                line.set_clip_box(box)
                line.draw(renderer)

    def _redraw_margins(self, renderer) -> None:
        """Ticks follow the copied picture. They are outside the bitmap, so
        the bitmap is left as it was and the margin is painted again."""
        canvas = self.mpl.canvas
        width, height = canvas.get_width_height()
        x0, y0, x1, y1 = self.ax.bbox.extents
        colour = self.mpl.get_facecolor()
        for rect in ((0, 0, width, y0), (0, y1, width, height - y1),
                     (0, y0, x0, y1 - y0), (x1, y0, width - x1, y1 - y0)):
            if rect[2] <= 0 or rect[3] <= 0:
                continue
            Rectangle((rect[0], rect[1]), rect[2], rect[3], transform=IdentityTransform(),
                      facecolor=colour, edgecolor="none", antialiased=False,
                      clip_on=False).draw(renderer)
        hidden = []
        for axis in (self.ax.xaxis, self.ax.yaxis):
            for gridline in axis.get_gridlines():
                hidden.append((gridline, gridline.get_visible()))
                gridline.set_visible(False)
        try:
            for spine in self.ax.spines.values():
                spine.draw(renderer)
            self.ax.xaxis.draw(renderer)
            self.ax.yaxis.draw(renderer)
            self.ax.title.draw(renderer)
            for twin in self._extra:
                for spine in twin.spines.values():
                    if spine.get_visible():
                        spine.draw(renderer)
                (twin.xaxis if self.figure.settings["flipped"] else twin.yaxis).draw(renderer)
        finally:
            for gridline, visible in hidden:
                gridline.set_visible(visible)

    def _clear_top(self) -> None:
        for artist in self._overlay_artists:
            artist.remove()
        self._overlay_artists = []
        if self._legend is not None:
            self._legend.remove()
            self._legend = None

    def _paint_top(self, scene: Scene, renderer) -> None:
        settings = self.figure.settings
        labelled = [artist for artist in self._artists if not artist.get_label().startswith("_")]
        if labelled and settings["legend"]:
            chrome = scene.chrome
            self._legend = self.ax.legend(
                handles=labelled, loc=_CORNERS.get(settings["legendCorner"], "best"),
                facecolor=chrome.ground[:3], edgecolor=chrome.axis_rule[:3],
                labelcolor=chrome.ink[:3])
            self._legend.set_zorder(10)
            self._legend.draw(renderer)
        self._overlay_artists = _overlay(self.figure, scene, self.ax)
        for artist in self._overlay_artists:
            artist.draw(renderer)

    def _reserve_foot(self) -> None:
        """Leave room under the pane for matplotlib's own tick and its label.

        The foot is the height of the type plus a short gap, which is where
        plotcore would set a number. Matplotlib hangs a tick and a pad below
        the rule as well, and the tick mark is centred on the rule, so half
        of it lies in the foot. A foot that stops at the type lets the rule,
        and the pane copied back over it, paint out the tick and the top of
        the digits."""
        import matplotlib as mpl
        from matplotlib.font_manager import FontProperties

        dpi = float(self.mpl.dpi or 100.0)
        points = FontProperties(size=mpl.rcParams["xtick.labelsize"]).get_size_in_points()
        font_px = points * dpi / 72.0
        pad_px = float(mpl.rcParams["xtick.major.pad"]) * dpi / 72.0
        # The mark is centred on the rule, so only half of it is ink in the
        # foot, but the number is seated a whole mark and a pad below the rule.
        tick_px = float(mpl.rcParams["xtick.major.size"]) * dpi / 72.0
        gap = float(self.figure.style["gapS"])
        # Two pixels under the descent, clear of the canvas edge.
        needed = max(1.2 * float(self.figure.style["tickPixels"]),
                     font_px + pad_px + tick_px + 2.0 - gap)
        renderer = self.mpl.canvas.get_renderer()
        prop = FontProperties(size=points)

        def width(text: str, role: TextRole) -> float:
            if role == TextRole.TICK:
                measured, _height, _descent = renderer.get_text_width_height_descent(
                    text, prop, False)
                return float(measured)
            return 0.6 * float(self.figure.style[_ROLES.get(role, "tickPixels")]) * len(text)

        def height(role: TextRole) -> float:
            if role == TextRole.TICK:
                return needed
            return 1.2 * float(self.figure.style[_ROLES.get(role, "tickPixels")])

        self.figure.set_text_measure(width, height)

    def _seat_foot_labels(self, scene: Scene) -> None:
        """Keep the tick and its number below the rule and on the canvas.

        The number is seated a whole mark and a pad below the rule. Counting
        only the ink, which is the outer half of that mark, leaves the digits
        running off the canvas, and pulling them up by the difference puts
        them under the rule."""
        canvas_h = self.mpl.canvas.get_width_height()[1]
        foot = canvas_h - (scene.area.y + scene.area.height)
        if foot <= 1.0:
            return
        flipped = bool(self.figure.settings["flipped"])
        axis = self.ax.yaxis if flipped else self.ax.xaxis
        ticks = axis.get_major_ticks()
        if not ticks:
            return
        dpi = float(self.mpl.dpi or 100.0)
        to_pt = 72.0 / dpi
        font_px = float(ticks[0].label1.get_fontsize()) / to_pt
        length_px = float(ticks[0].tick1line.get_markersize()) / to_pt
        pad_px = float(ticks[0].get_pad()) / to_pt
        # One pixel of the foot stays clear under the descent.
        overflow = length_px + pad_px + font_px + 1.0 - foot
        if overflow <= 0.0:
            return
        pad_px = max(0.0, pad_px - overflow)
        overflow = length_px + pad_px + font_px + 1.0 - foot
        if overflow > 0.0:
            length_px = max(0.0, length_px - overflow)
            overflow = length_px + pad_px + font_px + 1.0 - foot
        if overflow > 0.0:
            font_px = max(1.0, font_px - overflow)
            self.ax.tick_params(axis="y" if flipped else "x", which="both",
                                labelsize=font_px * to_pt)
        self.ax.tick_params(axis="y" if flipped else "x", which="both",
                            length=length_px * to_pt, pad=pad_px * to_pt)

    def _repaint(self, scene: Scene) -> None:
        self._overlay_artists = []
        self._legend = None
        self._artists = []
        for twin in self._extra:
            twin.remove()
        self._extra.clear()
        self.ax.cla()
        self.ax.set_navigate(False)
        self._artists = _paint(self.figure, scene, self.ax, self._extra, top=False)
        self._seat_foot_labels(scene)
        self._capturing = True
        try:
            self.mpl.canvas.draw()
            self._bg = self.mpl.canvas.copy_from_bbox(self.ax.bbox)
            self._anchor = scene.view
            self._anchor_scene = scene
            self._paint_top(scene, self.mpl.canvas.get_renderer())
            self.mpl.canvas.blit(self.mpl.bbox)
        finally:
            self._capturing = False

    # --- gestures ---------------------------------------------------------------

    def _pane(self, event) -> Optional[Tuple[float, float]]:
        """Where a matplotlib event falls on the figure's pane, or None when
        it is outside the axes."""
        if self._scene is None or event.x is None or event.y is None:
            return None
        # The axes are the pane, so a pixel of the event is a pixel of it.
        box = self.ax.get_window_extent()
        if box.width <= 0 or box.height <= 0:
            return None
        across = (event.x - box.x0) / box.width
        down = (box.y1 - event.y) / box.height
        area = self._scene.area
        return area.x + across * area.width, area.y + down * area.height

    @staticmethod
    def _modifiers(event) -> Modifiers:
        held = getattr(event, "modifiers", None)
        words = set(held) if held else set((event.key or "").replace("+", " ").split())
        out = Modifiers.NONE
        if "shift" in words:
            out |= Modifiers.SHIFT
        if words & {"ctrl", "control"}:
            out |= Modifiers.CONTROL
        if "alt" in words:
            out |= Modifiers.ALT
        return out

    @staticmethod
    def _button(event) -> Optional[Button]:
        return {1: Button.LEFT, 2: Button.MIDDLE, 3: Button.RIGHT}.get(int(event.button or 0))

    def _scroll(self, event) -> None:
        at = self._pane(event)
        if at is None:
            return
        self.figure.wheel(at[0], at[1], 120.0 * event.step, self._modifiers(event))
        self.redraw()

    def _press(self, event) -> None:
        at = self._pane(event)
        button = self._button(event)
        if at is None or button is None:
            return
        if event.dblclick:
            self.figure.double_click(at[0], at[1], button)
        else:
            self.figure.press(at[0], at[1], button, self._modifiers(event))
        self.redraw()

    def _motion(self, event) -> None:
        at = self._pane(event)
        if at is None:
            return
        self.figure.move(at[0], at[1], self._modifiers(event))
        self.redraw()

    def _release(self, event) -> None:
        at = self._pane(event)
        button = self._button(event)
        if at is None or button is None:
            return
        self.figure.release(at[0], at[1], button, self._modifiers(event))
        self.redraw()

    def _leave(self, _event) -> None:
        self.figure.leave()
        self.redraw()

    def show(self) -> None:
        import matplotlib.pyplot as plt

        self.redraw()
        plt.show()


__all__ = ["View", "draw"]
