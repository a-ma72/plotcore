// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: BSD-3-Clause

#include "plotcore/Palettes.hpp"

#include "plotcore/Format.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

// The values are Theme.qml's, token for token; the functions are Theme's and
// PlotSurface's of the same names. The argument for each is written over it,
// in the viewer's words, carried from the QML when the viewer came to draw
// through Figure.

namespace plotcore {
namespace {

// The neutral ramp, and the two signal colours paper has to answer for.
constexpr std::string_view kN0 = "#000000";
constexpr std::string_view kN3 = "#15181A";
constexpr std::string_view kN4 = "#1D2124";
constexpr std::string_view kN5 = "#282D31";
constexpr std::string_view kN6 = "#3A4045";
constexpr std::string_view kN7 = "#5A6268";
constexpr std::string_view kN8 = "#8B959B";
constexpr std::string_view kN9 = "#B9C2C7";
constexpr std::string_view kN10 = "#E4E8EA";
constexpr std::string_view kN11 = "#FFFFFF";
constexpr std::string_view kSig500 = "#FFFFFF";
constexpr std::string_view kCyan500 = "#4DE1FF";
constexpr std::string_view kCyan900 = "#0B3A47";
constexpr std::string_view kLineLight = "#DFE3E5";

int hexDigit(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return 0;
}

double channel(std::string_view hex, std::size_t at)
{
    return (hexDigit(hex[at]) * 16 + hexDigit(hex[at + 1])) / 255.0;
}

/// Equal as an eight-bit colour, which is what Qt.colorEqual compares.
bool sameColour(const Rgba& a, const Rgba& b)
{
    const auto byte = [](double v) {
        return static_cast<int>(jsRound(std::clamp(v, 0.0, 1.0) * 255.0));
    };
    return byte(a.red) == byte(b.red) && byte(a.green) == byte(b.green) &&
           byte(a.blue) == byte(b.blue) && byte(a.alpha) == byte(b.alpha);
}

std::vector<Rgba> stopsOf(std::initializer_list<std::string_view> hexes)
{
    std::vector<Rgba> out;
    out.reserve(hexes.size());
    for (const std::string_view hex : hexes) {
        out.push_back(colourOf(hex));
    }
    return out;
}

} // namespace

Rgba colourOf(std::string_view hex)
{
    if (!hex.empty() && hex.front() == '#') {
        hex.remove_prefix(1);
    }
    if (hex.size() == 6) {
        return Rgba::of(channel(hex, 0), channel(hex, 2), channel(hex, 4));
    }
    if (hex.size() == 8) {
        return Rgba::of(channel(hex, 2), channel(hex, 4), channel(hex, 6), channel(hex, 0));
    }
    return {};
}

std::string hexOf(const Rgba& colour)
{
    const auto byte = [](double v) {
        return static_cast<unsigned>(jsRound(std::clamp(v, 0.0, 1.0) * 255.0));
    };
    char text[12];
    if (byte(colour.alpha) == 255U) {
        std::snprintf(text, sizeof(text), "#%02x%02x%02x", byte(colour.red), byte(colour.green),
                      byte(colour.blue));
    }
    else {
        std::snprintf(text, sizeof(text), "#%02x%02x%02x%02x", byte(colour.alpha), byte(colour.red),
                      byte(colour.green), byte(colour.blue));
    }
    return text;
}

const std::vector<std::string>& paletteNames()
{
    static const std::vector<std::string> names{"okabe-ito", "tol bright", "tol muted", "spectrum",
                                                "safe"};
    return names;
}

const std::vector<std::string>& rampNames()
{
    static const std::vector<std::string> names{"grayscale", "viridis", "inferno", "hot", "cool"};
    return names;
}

// The categorical palettes for plotted lines (Theme.palettesFor).
//
// A ramp answers "how far along is this line". A palette answers "which
// line is this". They are different questions, and the ramps above are the
// wrong instrument for the second one: a ramp puts its neighbours next to
// each other by construction, so lines 7 and 8 of a viridis differ by a
// shade nobody can name and the legend is the only thing separating them.
// A palette spends its colours the other way -- every entry as far from
// every other as the space allows -- which is what lets a stroke be
// identified on its own, without reading a key. That is what a plot of
// named lines actually needs, so it is what the plot opens on.
//
// Both palettes are generated rather than borrowed, by farthest-point
// selection in CIELAB under CIEDE2000: each colour in turn is the one
// furthest from the ground it is drawn on, from the accent drawn over it,
// and from every colour already taken. That ordering is the point -- the
// first k entries are near-maximally separated for *any* k, so six lines
// are as distinguishable as twenty and simply take the first six.
//
// The two scopes are the same hues solved twice, for the rule the light
// scope above is solved for: a colour reads against white as strongly as
// its counterpart reads against black. A line therefore keeps its identity
// when the theme flips -- the green one is still the green one -- and
// changes only its weight. Where a hue has no deep form that is still a
// colour (the cyans and the yellows, which go grey or olive before they go
// dark) it is lightened until it does, which is the concession the signal
// colours already make at amber700 and red700.
//
// Every entry clears WCAG's 3:1 non-text contrast against its own scope's
// ground -- 3.9:1 at the very worst -- so no line is drawn in something
// the reader has to hunt for on either theme.
//
// The three published cycles below are here on a different argument, and
// it beats that one. `spectrum` and `safe` are solved for this
// application's two grounds and for nothing else, which means a figure
// drawn here and the same figure redrawn by a collaborator in matplotlib
// or R are two different pictures of one dataset. Okabe-Ito and Paul Tol's
// sets are the ones a reader's field already agrees on -- they are what
// journals ask for and what every plotting library ships -- so a plot
// exported from here lands in a paper beside plots that match it. That is
// worth more than a contrast figure, so they are kept at their published
// values, entry for entry, and are what this plot opens on. Okabe-Ito's
// order has one departure, argued where it is made: its achromatic entry
// is drawn last rather than first, so that a plot of one line opens on a
// colour.
//
// What it costs, stated plainly so that nobody "fixes" it later: these
// were designed for ink on paper, and it shows in the light scope. Okabe-
// Ito's yellow carries about 1.25:1 against white and Tol bright's yellow
// and grey are little better. On the dark theme they are all comfortable.
// Deepening them would make the light theme easier and would also mean
// that "Okabe-Ito" here is not Okabe-Ito, which is the whole of what they
// are being taken for. A reader who wants a cycle solved for a screen has
// `spectrum` and `safe` one pick away.
//
// A *function* of which ground rather than a plain object, and that is the
// whole of what lets the picture that leaves be drawn in the light scope
// while the reader stays in the dark one. Three of these five differ
// between the scopes and two do not; writing the light set out a second
// time for the picture would be five palettes kept in two places, and the
// two that do not differ would be the ones to drift. See `paperPalettes`
// below, which is this called with the other answer.
std::optional<std::vector<Rgba>> palette(std::string_view name, bool onDark)
{
    if (name == "okabe-ito") {
        // Okabe and Ito's Color Universal Design set, the eight of it.
        //
        // Black is the one entry that cannot survive the dark scope -- it is
        // the ground -- so it is drawn at signal white there, which is the
        // same substitution every dark-theme port of this palette makes. The
        // other seven are identical in both scopes: they were chosen to hold
        // apart under all three dichromacies, and re-solving them for a black
        // ground would be a different palette wearing this one's name.
        //
        // The achromatic entry is drawn *last* rather than first, and that
        // is the one departure from the published order in this table. Its
        // colours are the published colours and its cycle is the published
        // cycle; what moved is where the set starts. Published, it starts
        // on black -- which is fine in a figure of eight lines and is the
        // wrong opening for a plot that usually has one, because the first
        // thing this application draws is then the only line on the pane
        // and it is drawn in an ink the reader cannot tell from the
        // chrome. So the cycle opens on the orange and closes on the
        // black. A reader comparing a figure from here with one from
        // matplotlib still finds the same eight colours saying the same
        // eight things; they are offset by one.
        return stopsOf({"#E69F00", "#56B4E9", "#009E73", "#F0E442", "#0072B2", "#D55E00", "#CC79A7",
                        onDark ? kN11 : kN0});
    }
    if (name == "tol bright") {
        // Paul Tol's bright scheme: seven, colour-blind safe, and the one most
        // often reached for where a handful of lines need naming.
        return stopsOf(
            {"#4477AA", "#EE6677", "#228833", "#CCBB44", "#66CCEE", "#AA3377", "#BBBBBB"});
    }
    if (name == "tol muted") {
        // ...and his muted scheme, nine of them, for the same job where the
        // bright one is louder than the page wants. Tol's pale grey for "data
        // that is not a category" is deliberately left out: every entry here
        // is a line, and a line drawn in the colour reserved for "none of
        // these" is a line saying something it does not mean.
        return stopsOf({"#CC6677", "#332288", "#DDCC77", "#117733", "#88CCEE", "#882255", "#44AA99",
                        "#999933", "#AA4499"});
    }
    if (name == "spectrum") {
        // Twenty colours, no two closer than 13.1 dE00 in either scope. For
        // scale: matplotlib's tab20 manages 12.1 across its twenty, and does
        // it against one ground rather than two.
        if (onDark) {
            return stopsOf({"#B64EF5", "#F6330C", "#009700", "#0089C4", "#C99400",
                            "#FF9DBB", "#00CFC6", "#ACB5FF", "#64F44D", "#FFA379",
                            "#FF005E", "#F09AFF", "#6173FF", "#808700", "#F200AE",
                            "#009276", "#C76700", "#12C8FF", "#00B46A", "#B3C300"});
        }
        return stopsOf({"#9C33DB", "#D01F00", "#007C00", "#0089C3", "#6C4E00", "#7D003F", "#006A65",
                        "#003989", "#063A00", "#6F2500", "#D4004C", "#6C0081", "#085AFC", "#696F00",
                        "#C90090", "#009276", "#A55500", "#006684", "#006237", "#444B00"});
    }
    if (name == "safe") {
        // Ten that hold apart under protanopia, deuteranopia and tritanopia as
        // well as under ordinary vision: 7.8 dE00 at the worst, simulated
        // four ways in both scopes. tab10 falls to 1.9 on the same measure and
        // Okabe-Ito's seven reach 8.7 -- but Okabe-Ito's yellow carries 1.3:1
        // against white, which is a line the light theme cannot show at all.
        //
        // Shorter than the spectrum on purpose. Colours that survive all three
        // dichromacies at once are a small set, and padding the list would
        // only be a longer list of colours some readers cannot tell apart.
        if (onDark) {
            return stopsOf({"#0088CB", "#B37400", "#FFAE7F", "#CCB5FF", "#FB0080", "#00F3BD",
                            "#FF8798", "#00A98C", "#798DFF", "#FC43E7"});
        }
        return stopsOf({"#007BB8", "#945F00", "#602800", "#001AB4", "#D00069", "#00523E", "#930036",
                        "#008B73", "#0050C5", "#A20094"});
    }
    return std::nullopt;
}

std::optional<std::vector<Rgba>> ramp(std::string_view name)
{
    if (name == "grayscale") {
        return stopsOf({kN5, kN7, kN9, kN10, kN11});
    }
    if (name == "viridis") {
        return stopsOf({"#440154", "#3B528B", "#21918C", "#5EC962", "#FDE725"});
    }
    if (name == "inferno") {
        return stopsOf({"#000004", "#420A68", "#932667", "#DD513A", "#FCA50A", "#FCFFA4"});
    }
    if (name == "hot") {
        return stopsOf({"#0B0000", "#E60000", "#FF7A00", "#FFF34D", "#FFFFFF"});
    }
    if (name == "cool") {
        return stopsOf({"#00FFFF", "#FF00FF"});
    }
    return std::nullopt;
}

Rgba mix(const Rgba& from, const Rgba& to, double amount)
{
    const double t = std::max(0.0, std::min(1.0, amount));
    return Rgba::of(from.red + (to.red - from.red) * t, from.green + (to.green - from.green) * t,
                    from.blue + (to.blue - from.blue) * t,
                    from.alpha + (to.alpha - from.alpha) * t);
}

// A colour `position` of the way along `stops`, interpolated in RGB
// (Theme.rampColor). A function rather than a table because the number of
// lines is not known until the plot is drawn: five stops have to serve two
// lines and five hundred. Linear in RGB rather than in HSV -- HSV walks the
// long way round the wheel between two stops and invents hues that are in
// neither.
Rgba rampColor(const std::vector<Rgba>& stops, double position)
{
    if (stops.empty()) {
        return colourOf(kSig500);
    }
    if (stops.size() == 1) {
        return stops.front();
    }
    const double at =
        std::max(0.0, std::min(1.0, position)) * static_cast<double>(stops.size() - 1);
    const auto lower = static_cast<std::size_t>(std::floor(at));
    const std::size_t upper = std::min(lower + 1, stops.size() - 1);
    return mix(stops[lower], stops[upper], at - static_cast<double>(lower));
}

// The colour line `position` takes from the palette `stops`, which repeats
// (Theme.categoricalColor). rampColor's counterpart, and the difference
// between them is the whole difference between the two kinds of cycle: a
// ramp is asked for a place along a continuum and interpolates to reach it, a
// palette is asked which entry a line *is* and can only hand one back.
//
// Repeating rather than running out is what makes two hundred lines drawable
// at all. The twenty-first line takes the first colour again, and the legend
// beside it is what tells the two apart -- the bargain every plotting library
// makes, and a better one than silently drawing the rest in grey. The modulo
// stays positive because reversing a cycle counts backwards from its end.
Rgba categoricalColor(const std::vector<Rgba>& stops, double position)
{
    if (stops.empty()) {
        return colourOf(kSig500);
    }
    const auto n = static_cast<long long>(stops.size());
    const auto at = static_cast<long long>(jsRound(position));
    return stops[static_cast<std::size_t>(((at % n) + n) % n)];
}

// One colour a reader's own settings put on a line, as it must be drawn on
// paper (Theme.paperColor).
//
// The cycles have a light-scope table of their own; this is for the three
// colours that are not a cycle at all -- the "same" colour, the two ends of a
// "range", and a line the reader gave a colour of its own -- because those
// are *values* by the time they get here and there is no binding left to
// re-resolve. So they are mapped by value, and only the two a token can have
// put there. The first is not a nicety: the accent is signal white on the
// dark theme, and a white line on paper is not a faint line -- it is no line
// at all. That substitution is the exact mirror of the one okabe-ito makes in
// the other direction: a colour that *is* the ground is not a line.
//
// What it costs is small: a reader who picks signal white or signal cyan by
// hand gets paper's version of it rather than the colour they pointed at --
// the answer the light theme would give them, and legible on a page.
Rgba paperColor(const Rgba& colour)
{
    if (sameColour(colour, colourOf(kSig500))) {
        return colourOf(kN0);
    }
    if (sameColour(colour, colourOf(kCyan500))) {
        return colourOf(kCyan900);
    }
    return colour;
}

LineColours LineColours::defaults(bool onDark)
{
    LineColours colours;
    colours.single = colourOf(onDark ? kSig500 : kN0);
    colours.rangeFrom = colours.single;
    colours.rangeTo = colourOf(onDark ? kCyan500 : kCyan900);
    return colours;
}

LineColours LineColours::drawn(bool onDark) const
{
    if (custom) {
        return *this;
    }
    LineColours out = *this;
    const LineColours ground = defaults(onDark);
    out.single = ground.single;
    out.rangeFrom = ground.rangeFrom;
    out.rangeTo = ground.rangeTo;
    return out;
}

// The colour of line `series`, drawn `position` of `count`.
//
// Three numbers for one line, because the two kinds of cycle ask
// different questions of it. `series` is *which* line it is -- its own
// index in the legend, the number that names it whether it is drawn or
// not -- and it is the whole of a palette's answer. `position` and
// `count` place it among the lines currently drawn, which is what a map
// needs and what a palette ignores: a share of a continuum only exists
// once you know how many shares there are. The rest of this note is about
// the maps.
//
// The lines sit at the *middles* of `count` equal shares of the map
// rather than at its ends: line i of n is drawn at (i + 1) / (n + 1), so
// one line takes the middle colour, two take the thirds, three the
// quarters, and so on. The ends of the map are never reached.
//
// That is the point of it. Every perceptual ramp runs from something very
// dark to something very pale, and both of those are a line nobody can
// see: on this plot's true-black ground the first line of a viridis was
// very nearly invisible, and on a white one the last line of a hot is.
// Spreading the lines end to end spent the two worst colours in the map
// on two of the lines every time, and did it worst in the case that
// matters most -- a single line, which took the very first colour and
// nothing else.
//
// The reader's own band (colorFrom .. colorTo) still applies on top, so
// narrowing the map narrows what these shares are taken out of.
//
// `paper` is the fourth number and the only one that is not about which
// line this is: it asks for the colour the *light* scope would draw,
// whatever scope is on screen. That is what a publication export is --
// see the paper block in Theme -- and it is asked here rather than
// substituted afterwards because "the same picture in the other scope"
// is a property of every branch below and not of the answer. Until 0.6.4
// the picture that left was every stroke in one black ink, which is the
// one thing a colour cycle exists to not be: six traces pasted into a
// document were six identical strokes with a caption naming colours that
// were not in the picture.
//
// A colour the reader gave this line beats every cycle, and beats it
// first: a cycle answers "which line is this" for lines that are alike,
// and the lines of a custom tab were each put there on purpose. The
// override is per line and nothing else moves, which is the same
// promise a palette makes about its own index and for the same reason
// -- a reader watching one stroke must not see it change under them
// because they said something about another.
//
// Asked of the plot rather than held here, because both plots are
// drawn by this file and only one of them has any: DatasetPlot answers
// "no" for every line. Plot tab lines are rows or columns of one
// dataset and their place in the table is what identifies them.
//
// A palette is asked which line this is rather than how far along it
// sits, so none of the arithmetic below applies to one: not the shares
// -- there is no continuum to take shares of -- and not the reader's
// band. The line's own index is the answer, counted along a cycle that
// repeats, and it does not move when a line is added or taken away.
// That is the second thing a palette buys over a map: on a map every
// line changes colour when one of them is unticked.
//
// `series` and not `position`, which is the whole of that promise.
// Asked where the line sat among the drawn ones, a palette kept the
// promise only until the reader unticked something: hiding the first
// of five lines handed the second line the first one's colour, and the
// reader watching one stroke saw it change under them -- the exact
// failure a palette is here to not have.
//
// Across the reader's own slice of the map rather than across the
// whole of it. Untouched that slice is the whole of it, so the
// arithmetic is a no-op until they say otherwise.
//
// The ramps are the one part of this that has no second scope: they
// are named colour maps kept as they are defined elsewhere, so a
// viridis on paper is the viridis a reader in the light theme sees and
// the viridis matplotlib draws. See Theme.colorRamps.
Rgba seriesColor(const LineColours& chosen, int series, int position, int count, bool paper,
                 bool onDark, const std::optional<Rgba>& own)
{
    const LineColours colours = chosen.drawn(onDark);
    if (own.has_value() && own->valid) {
        return paper ? paperColor(*own) : *own;
    }
    if (colours.mode == "same") {
        return paper ? paperColor(colours.single) : colours.single;
    }
    // A palette is asked which line this is, by `series` and not `position`,
    // so hiding one line does not hand another its colour.
    if (const std::optional<std::vector<Rgba>> stops =
            palette(colours.mode, paper ? false : onDark)) {
        const double at = colours.reversed ? static_cast<double>(stops->size()) - 1.0 - series
                                           : static_cast<double>(series);
        return categoricalColor(*stops, at);
    }
    double at = count > 0 ? (position + 1.0) / (count + 1.0) : 0.5;
    if (colours.reversed) {
        at = 1.0 - at;
    }
    at = colours.from + at * (colours.to - colours.from);
    if (colours.mode == "range") {
        return mix(paper ? paperColor(colours.rangeFrom) : colours.rangeFrom,
                   paper ? paperColor(colours.rangeTo) : colours.rangeTo, at);
    }
    const std::optional<std::vector<Rgba>> stops = ramp(colours.mode);
    return rampColor(stops.value_or(std::vector<Rgba>{}), at);
}

// How strongly a line is drawn. One line has nothing to separate from and
// takes full strength; a bundle separates by overlap and is drawn under
// it. A highlighted line comes forward and the rest go further back, so
// the one being followed is the one that reads.
//
// Only a bundle is drawn under. A custom tab's lines used to be as well,
// so adding a second line to one dimmed the first to 0.55 on the dark
// theme -- two lines chosen one at a time, each drawn fainter than it was
// alone, when there was no pile-up to show.
double seriesOpacity(int series, int count, int highlighted, bool bundle, bool onDark)
{
    // The two scopes are nowhere near each other, and the arithmetic is why
    // (Theme.plotSeriesOpacity). A translucent line is its colour mixed with
    // the ground: toward black that is a dimmer version of the colour, and dim
    // on black is still a line; toward white it floods the colour with white,
    // and pale on white is nothing. Measured: the palettes' weakest entry reads
    // at 3.4:1 through 0.55 on black and at 1.7:1 through 0.55 on white, which
    // is a line the reader has to hunt for. 0.95 brings the light scope back to
    // the dark one's 3.4:1 -- so the light theme keeps a little of the piling
    // up and spends the rest on being legible, the trade a white ground forces.
    const double bundled = onDark ? 0.55 : 0.95;
    if (highlighted >= 0) {
        return series == highlighted ? 1.0 : bundled / 2.0;
    }
    return bundle && count > 1 ? bundled : 1.0;
}

// How heavily. A line being followed is drawn thicker as well as brighter:
// at a hairline, opacity alone is not enough to pick one out of fifty.
double seriesWidth(int series, int highlighted, double lineWidth)
{
    return series == highlighted ? lineWidth * 2.0 : lineWidth;
}

Chrome chromeFor(bool onDark, bool paper)
{
    Chrome chrome;
    if (paper) {
        // PlotPicture's publication overrides: the light scope, with the
        // legend on the page itself rather than on a veil, ruled at the
        // major rule's step and its count written at the axis's.
        //
        // The light scope named *without* the theme's switch in front of it,
        // and that is the whole of why there is a paper chrome at all. A plot
        // exported for publication is drawn on paper -- white, or the page's
        // own colour through a clear ground -- and the reader who asks for it
        // is usually sitting in the dark theme. None of these is a new colour:
        // each is the light-scope value of the one it stands in for, which is
        // what keeps a publication export and the light theme one picture.
        chrome = Chrome{colourOf(kN11),
                        colourOf(kN0),
                        colourOf(kLineLight),
                        colourOf(kN9),
                        colourOf(kN6),
                        colourOf(kN0),
                        colourOf(kN0),
                        colourOf(kN6),
                        Rgba::of(1.0, 1.0, 1.0, 0.0),
                        colourOf(kN9),
                        Rgba::of(0.0, 0.0, 0.0, 0.12),
                        colourOf(kN0)};
    }
    else if (onDark) {
        chrome = Chrome{colourOf(kN0),
                        colourOf(kN11),
                        colourOf(kN4),
                        colourOf(kN6),
                        colourOf(kN9),
                        colourOf(kSig500),
                        colourOf(kN11),
                        colourOf(kN8),
                        Rgba::of(0.0, 0.0, 0.0, 0.78),
                        colourOf(kN4),
                        Rgba::of(1.0, 1.0, 1.0, 0.14),
                        colourOf(kSig500)};
    }
    else {
        chrome = Chrome{colourOf(kN11),
                        colourOf(kN0),
                        colourOf(kLineLight),
                        colourOf(kN9),
                        colourOf(kN6),
                        colourOf(kN0),
                        colourOf(kN3),
                        colourOf(kN7),
                        Rgba::of(1.0, 1.0, 1.0, 0.84),
                        colourOf(kLineLight),
                        Rgba::of(0.0, 0.0, 0.0, 0.12),
                        colourOf(kN0)};
    }
    return chrome;
}

} // namespace plotcore
