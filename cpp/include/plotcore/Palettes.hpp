// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Which colour each line is drawn in, and what the picture around it is drawn
// in -- the viewer's Theme.qml and PlotSurface.seriesColor, as data and
// arithmetic.
//
// The palettes are two kinds of thing, held to two rules (H5Scope's CLAUDE.md,
// invariant 5). `spectrum` and `safe` were generated against the viewer's two grounds,
// so every entry clears 3:1 on both. `okabe-ito`, `tol bright` and `tol muted`
// are the published cycles at their published values, so a figure from here
// and one from matplotlib agree about which line is which -- with Okabe-Ito's
// black drawn last rather than first, and drawn white on the dark ground,
// where black is the ground itself. H5Scope's tst_plotcore_parity.qml holds
// every stop here to Theme.qml's.
//
// "Paper" is the light scope: a picture exported for a page is drawn as the
// light theme would draw it, whatever is on screen. A palette is read against
// the other ground; a colour that is already a value goes through paperColor().

#include "plotcore/Types.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace plotcore {

/// `#RRGGBB` or `#AARRGGBB`, as Qt reads a colour string.
[[nodiscard]] Rgba colourOf(std::string_view hex);
/// The colour as `#rrggbb`, or `#aarrggbb` when it is not opaque.
[[nodiscard]] std::string hexOf(const Rgba& colour);

/// The categorical cycles, in the order a settings panel offers them.
[[nodiscard]] const std::vector<std::string>& paletteNames();
/// The continuous maps, in the same sense.
[[nodiscard]] const std::vector<std::string>& rampNames();

/// One cycle's stops as they are drawn on a dark ground or a light one, or
/// nothing for a name that is not a cycle.
[[nodiscard]] std::optional<std::vector<Rgba>> palette(std::string_view name, bool onDark);
/// One map's stops, which are the same in either scope; nothing for a name
/// that is not a map.
[[nodiscard]] std::optional<std::vector<Rgba>> ramp(std::string_view name);

/// `Theme.mix`: channel by channel, `amount` clamped to 0..1.
[[nodiscard]] Rgba mix(const Rgba& from, const Rgba& to, double amount);
/// `Theme.rampColor`: `position` from 0 to 1 along the stops.
[[nodiscard]] Rgba rampColor(const std::vector<Rgba>& stops, double position);
/// `Theme.categoricalColor`: stop `position`, round the cycle.
[[nodiscard]] Rgba categoricalColor(const std::vector<Rgba>& stops, double position);
/// `Theme.paperColor`: the accent and the info colour of the dark scope, as a
/// page draws them. Every other colour is itself.
[[nodiscard]] Rgba paperColor(const Rgba& colour);

/// The colours a reader chooses for the lines, as PlotSurface holds them.
struct LineColours
{
    /// A cycle, a map, "same" for one colour, or "range" for a mix between
    /// two.
    std::string mode = "okabe-ito";
    Rgba single;
    Rgba rangeFrom;
    Rgba rangeTo;
    bool reversed = false;
    /// The reader's slice of a map, from 0 to 1.
    double from = 0.0;
    double to = 1.0;
    /// Whether `single`, `rangeFrom` and `rangeTo` were chosen, which is what
    /// decides whether they are drawn: until they are, the ground's defaults
    /// are, so a line nobody coloured follows the ground. The viewer's
    /// PlotSurface.colorsCustom, for its reason.
    bool custom = false;

    /// The defaults the viewer opens on, for a dark ground or a light one.
    [[nodiscard]] static LineColours defaults(bool onDark);
    /// These colours as they are drawn on that ground: the chosen ones, or
    /// the ground's defaults when none were chosen.
    [[nodiscard]] LineColours drawn(bool onDark) const;
};

/// `PlotSurface.seriesColor`: the colour line `series` is drawn in, being
/// `position` of `count` drawn. A colour the reader gave the line beats every
/// cycle; `paper` asks for the picture a page would draw.
[[nodiscard]] Rgba seriesColor(const LineColours& colours, int series, int position, int count,
                               bool paper, bool onDark,
                               const std::optional<Rgba>& own = std::nullopt);

/// `PlotSurface.seriesOpacity` and `seriesWidth`.
[[nodiscard]] double seriesOpacity(int series, int count, int highlighted, bool bundle,
                                   bool onDark);
[[nodiscard]] double seriesWidth(int series, int highlighted, double lineWidth = 1.0);

/// What the picture around the lines is drawn in. `PlotFrame`'s six colour
/// properties, with their Theme defaults and their paper overrides -- and
/// then what PlotOverlayLegend and PlotBand are drawn in, with PlotPicture's
/// overrides of the legend's for paper.
struct Chrome
{
    Rgba ground;
    /// The ink on the plot: the exact inverse of the ground under it
    /// (Theme.plotInk).
    ///
    /// Everywhere else in the viewer a label is a secondary text colour, and
    /// that is right -- a form is mostly chrome, and chrome that reads as
    /// loudly as the value beside it competes with the value. A plot is not
    /// that. Its ground is the one surface at pure black or pure white, and
    /// everything written on it -- the numbers on the ticks, the names of the
    /// axes, the title -- is a *reading* rather than a label for one. So it is
    /// set at the full step, the opposite end of the neutral ramp: 21:1, the
    /// most the design can spend, on the one picture a reader takes away and
    /// looks at somewhere else.
    Rgba ink;
    Rgba ruleMinor;
    Rgba ruleMajor;
    Rgba axisRule;
    Rgba cursorInk;
    /// What is written on a veil: the legend's names and the band's numbers.
    /// Theme.textPrimary.
    Rgba text;
    /// What is said in passing: the legend's count of the lines it leaves
    /// out. Theme.textDisabled.
    Rgba faint;
    /// The ground under a legend and under the band's numbers, through which
    /// a reader can see that a stroke runs on. Theme.plotLegendGround, and
    /// clear on paper, where the page is the ground.
    ///
    /// Opaque would be simpler and is wrong: a legend in a corner covers part
    /// of the pane, and a reader has to be able to see that a stroke runs
    /// under it rather than stopping there. The band's numbers stand on the
    /// same veil on purpose (Theme.plotReadoutGround): what both hold up is a
    /// line of type on the picture, over a stroke as often as over the ground,
    /// and two veils at two weights would read as two different kinds of thing
    /// being said about one plot.
    Rgba veil;
    /// The legend's edge. Theme.border.
    Rgba legendEdge;
    /// The band a right drag draws, and its edge. Theme.plotBandFill and
    /// Theme.accent.
    ///
    /// An alpha rather than a mix: what is underneath a band is the picture
    /// itself -- the ground, the grid and however many strokes -- so there is
    /// no one colour to mix against. What matters is that the curve stays
    /// legible through it, because the band is drawn *while* the reader is
    /// deciding where to stop.
    Rgba bandFill;
    Rgba bandEdge;
};

/// The chrome on a dark ground or a light one, or on paper.
[[nodiscard]] Chrome chromeFor(bool onDark, bool paper);

} // namespace plotcore
