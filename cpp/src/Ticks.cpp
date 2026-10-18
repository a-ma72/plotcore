// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#include "plotcore/Ticks.hpp"

#include "plotcore/Format.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <numbers>

// Every function here is the one of the same name in H5Scope's
// src/qml/PlotFrame.qml, with JavaScript's arithmetic spelt out where C++'s
// differs: Math.round is jsRound, `%` on numbers is fmod, `.length` of a label
// is textLength, and a number is written by Format.hpp rather than by printf.
// The argument for each rule is written over it: the viewer's own, carried
// here from PlotFrame.qml when the viewer came to draw through Figure, and kept
// in its words -- so "this file" in one is PlotFrame, and a "frame" is its.

namespace plotcore {
namespace {

constexpr double kInfinity = std::numeric_limits<double>::infinity();

/// `Number(text)`, for the strings toPrecision() and toFixed() write.
double numberOf(const std::string& text)
{
    double value = std::numeric_limits<double>::quiet_NaN();
    const char* first = text.data();
    std::from_chars(first, first + text.size(), value);
    return value;
}

/// `Number.isInteger`.
bool isInteger(double x)
{
    return std::isfinite(x) && std::floor(x) == x;
}

} // namespace

// Whether `base` is a base at all.
//
// Only a number above one is. At exactly one the logarithm is a division
// by zero and every value on the axis would sit in the same place; below
// it the axis runs backwards, which is a different request from the one
// this answers. The same test gui::mappingOver makes, so that the chrome
// and the renderer cannot disagree about whether an axis exists.
//
// A test and not a clamp, and that is the honest shape for it: the legal
// bases are open at one, so there is no nearest legal value to correct a
// bad one to. What the panel does with a refusal is put the box back to
// the base in force, which says no rather than inventing a yes.
bool usableBase(double base)
{
    return std::isfinite(base) && base > 1.0;
}

// Where `value` sits on an axis running `low`..`high`, as a fraction from
// `low`.
//
// The one piece of arithmetic this whole file is arranged around, and the
// only place in it that knows a logarithmic axis exists. The ends of the
// axis are the window the surface resolved, with nothing between them and
// the data: this is the same subtraction the renderer does, over the same
// two numbers and through the same transform, which is what makes a tick
// land where the curve did.
//
// Zero for an axis with no span to divide by, and for a logarithmic one
// whose ends are not both above zero -- the same two refusals
// gui::mappingOver() makes, written out here because a chrome that
// answered a degenerate view differently from the renderer would be the
// tick lie in its quietest form.
double fractionOn(double value, double low, double high, bool logarithmic, double base)
{
    if (logarithmic && (!(low > 0.0) || !(high > 0.0) || !usableBase(base))) {
        return 0.0;
    }
    const double from = logarithmic ? logOf(low, base) : low;
    const double span = (logarithmic ? logOf(high, base) : high) - from;
    if (!std::isfinite(span) || !(span > 0.0)) {
        return 0.0;
    }
    return ((logarithmic ? logOf(value, base) : value) - from) / span;
}

// A round tick spacing giving roughly `target` ticks across `span`: 1, 2
// or 5 times a power of ten, which is what every axis in every plotting
// library settles on and what a reader can add up in their head.
double niceStep(double span, double target)
{
    if (!(span > 0.0)) {
        return 0.0;
    }
    const double raw = span / std::max(1.0, target);
    const double magnitude = std::pow(10.0, std::floor(std::log(raw) / std::numbers::ln10));
    const double scaled = raw / magnitude;
    return magnitude * (scaled <= 1.0 ? 1.0 : scaled <= 2.0 ? 2.0 : scaled <= 5.0 ? 5.0 : 10.0);
}

// Enough decimals to tell two ticks apart, and no more: a span of 100
// wants none, a span of a thousandth wants five. A fixed count prints
// either noise or "0.0" all the way up the axis. Taken off the visible
// span, so zooming in adds digits as the ticks close up.
int decimalsFor(double span)
{
    const double width = std::abs(span);
    if (!(width > 0.0)) {
        return 0;
    }
    const double wanted = std::ceil(-std::log(width) / std::numbers::ln10) + 2.0;
    return static_cast<int>(std::max(0.0, std::min(6.0, wanted)));
}

// One tick, written.
//
// The *span* decides whether to use an exponent, not the value. That is
// the distinction that matters on an axis with a large offset and a small
// range -- a time base of seconds since 1970 spanning five minutes -- where
// every tick rounds to 1.7e+9 and an axis of eight identical labels says
// nothing at all. Judged by the span, that axis prints whole seconds and
// the ticks differ; an axis that really does cross decades gets the
// exponent, and its ticks differ too.
//
// Zero written as an exponent is "0.0e+0", which is six characters
// that mean nothing the first one did not.
//
// As many figures as it takes to tell this tick from the next one,
// which is how far the axis sits above its span -- the argument
// decimalsFor makes, in significant figures rather than places.
// One figure was right only while the value and the span were of a
// size: between 1.0001 and 1.0002, which is where a logarithmic axis
// zoomed inside a decade ends up, every tick read "1.0e+0".
//
// Of the axis and not of the value, so that every number on it has
// as many figures as every other. Asked of each value, the count
// stepped up wherever a tick passed the span: an axis of four
// million numbered "3.00e+6" beside "5.000e+6".
std::string labelFor(double value, double low, double high)
{
    const double width = std::abs(high - low);
    if (width >= 1e6 || (width > 0.0 && width < 1e-4)) {
        if (value == 0.0) {
            return "0";
        }
        // Of the axis and not of the value, so every number on it has as many
        // figures as every other: asked of each value, the count stepped up
        // wherever a tick passed the span.
        const double magnitude = std::max(std::abs(low), std::abs(high));
        const double figures = std::ceil(std::log10(magnitude / width)) + 2.0;
        return toExponential(value, static_cast<int>(std::max(1.0, std::min(15.0, figures))));
    }
    return toFixed(value, decimalsFor(high - low));
}

// A decimal with its trailing zeros taken off, and the point with them.
//
// An exponent keeps its own part, so "1.00e+7" comes back as "1e+7".
std::string trimmed(const std::string& text)
{
    const std::size_t at = text.find('e');
    std::string mantissa = at == std::string::npos ? text : text.substr(0, at);
    if (mantissa.find('.') == std::string::npos) {
        return text;
    }
    // `.replace(/\.?0+$/, "")`: a run of trailing zeros, and the point before
    // it if that is what they leave at the end.
    const std::size_t kept = mantissa.find_last_not_of('0');
    if (kept + 1 < mantissa.size()) {
        mantissa.resize(kept + 1);
        if (!mantissa.empty() && mantissa.back() == '.') {
            mantissa.pop_back();
        }
    }
    return mantissa + (at == std::string::npos ? std::string() : text.substr(at));
}

// One tick on a logarithmic axis, written.
//
// A span decides nothing here, and that is the difference. On a linear
// axis every tick is the same distance from the next and they differ in
// their last digits, so what has to be written is exactly as much as tells
// two of them apart. On a logarithmic one each tick is a *multiple* of the
// one before, so they differ in their magnitudes and never in a trailing
// digit -- 0.001 and 100000 are ticks of one axis and each is written the
// way it would be written alone.
//
// Three significant figures, with the zeros a round number leaves taken
// off again: every tick this is asked about is a small multiple of a power
// of ten and comes back exact, and a band's readout asking about a value
// between two of them gets the figures it deserves.
//
// Past either end of what a reader takes in at a glance, a number is an
// exponent or it is a row of zeros.
std::string logLabelFor(double value)
{
    if (!(value > 0.0) || !std::isfinite(value)) {
        return numberText(value);
    }
    const double exponent = std::floor(std::log10(value));
    if (exponent >= 6.0 || exponent < -4.0) {
        return trimmed(toExponential(value, 2));
    }
    return trimmed(toFixed(value, static_cast<int>(std::max(0.0, std::min(8.0, 2.0 - exponent)))));
}

// A value on an axis running `low`..`high`, written the way that axis
// writes its own ticks. The one entry point for anything outside this
// file -- see PlotSurface.xNumber, which the band's readout goes through.
std::string axisLabel(double value, double low, double high, bool logarithmic)
{
    return logarithmic ? logLabelFor(value) : labelFor(value, low, high);
}

// The round values in `low`..`high`, stepping by `step`.
//
// Counted from a first tick rather than accumulated, because adding a
// step to itself a hundred times is a hundred roundings and the last tick
// comes out at 99.99999999999999. The ceiling on the count is not a
// design choice -- it is what stops a span that has gone to zero or NaN
// under a bad binding from asking for an unbounded Repeater.
//
// Both ends with a billionth of a step to spare, which is less than
// any tick could be drawn apart by and more than a multiplication can
// round by: 1 + 5 x 0.0002 is 1.0010000000000001, and without it an
// axis running 1 to 1.001 lost the tick at its own top.
std::vector<double> ticksBetween(double low, double high, double step)
{
    std::vector<double> found;
    if (!(step > 0.0) || !(high > low)) {
        return found;
    }
    const double first = std::ceil(low / step - 1e-9);
    for (int i = 0; i < 512; ++i) {
        const double value = (first + i) * step;
        if (value > high + step * 1e-9) {
            break;
        }
        found.push_back(value);
    }
    return found;
}

// matplotlib's `get_tick_space`: how many labels an axis has room for,
// from its length.
//
// Two lines of type per label up the side, and three characters' worth
// along the bottom, where a label is as wide as it is long -- matplotlib's
// own two constants over its own font size. Clipped to 2..9 the way
// `LogLocator` clips it, which is the whole range it ever asks for: never
// fewer than two (one number says nothing about a scale) and never more
// than nine (a power of ten per label past that is a column of them).
int logTickRoom(double extent, bool vertical, double fontPixels)
{
    const double per = (vertical ? 2.0 : 3.0) * fontPixels;
    const double room = std::floor(std::max(0.0, extent) / per);
    return static_cast<int>(std::max(2.0, std::min(9.0, std::isnan(room) ? 9.0 : room)));
}

// Whether `x` is a whole number, to within what a logarithm can be off
// by. matplotlib's `_is_close_to_int`, and its tolerance.
bool nearInteger(double x)
{
    return std::abs(x - jsRound(x)) < 1e-10;
}

// What a power holds: `arange(2, base)`. Empty below three, which is
// matplotlib's rule and not an accident of it -- a power of two doubles
// straight to the next with no whole multiple of itself between, and e,
// which would hold a 2, is under three too.
std::vector<double> logSubs(double base)
{
    std::vector<double> subs;
    if (base < 3.0) {
        return subs;
    }
    for (double m = 2.0; m < base && subs.size() < 64; ++m) {
        subs.push_back(m);
    }
    return subs;
}

// `LogLocator.tick_values`, over `low`..`high`, asking for about
// `request` numbers.
//
// Returns the majors and the minors *in view*, the stride the majors were
// taken at, and whether the minors are the linear fallback. The majors
// are built as `base ** exponent` rather than by repeated multiplication,
// so that a power of ten is that power of ten exactly and the label it
// carries sits on its own rule.
//
// *The number of ticks*: the smallest stride that brings them under
// what was asked for, and then as many as that stride really gives.
//
// *The stride*: the largest one that still gives this many, so
// the unticked space at the ends is as small as it can be...
//
// ...and *the offset*: at a multiple of the stride if that gives
// the same count, which is what puts 10^0 on the axis whenever it
// can be; else the smallest offset that does.
//
// One tick or none in view says nothing about the scale, so the
// minors become the round values a linear axis would have carried.
//
// A power still in view stays, and the round values within half a
// step of it give way to it. On base ten that is only ever the one
// they coincide with, since a power of ten is a multiple of every
// linear step inside it; on base two, whose powers are not round
// decimals, it is what keeps 1.4, 1.49 and 1.5 from being printed on
// top of one another.
LogLocated logLocate(double low, double high, double base, int request, int target)
{
    if (!(low > 0.0) || !(high > low) || !usableBase(base)) {
        return {};
    }
    const double efmin = logOf(low, base);
    const double efmax = logOf(high, base);
    const double emin = std::ceil(efmin - 1e-10);
    const double emax = std::floor(efmax + 1e-10);
    const double avail = emax - emin + 1.0;

    double wanted = std::max(2.0, static_cast<double>(request));
    double stride = std::floor(avail / (wanted + 1.0)) + 1.0;
    const double got = std::ceil(avail / stride);
    if (got <= wanted) {
        wanted = got;
    }
    std::vector<double> decades;
    if (wanted <= 0.0) {
        decades = {emin - 1.0, emax + 1.0};
        stride = decades[1] - decades[0];
    }
    else if (wanted == 1.0) {
        const double mid = jsRound((efmin + efmax) / 2.0);
        stride = std::max(mid - (emin - 1.0), (emax + 1.0) - mid);
        decades = {mid - stride, mid, mid + stride};
    }
    else {
        stride = std::floor((avail - 1.0) / (wanted - 1.0));
        if (stride < avail / wanted) {
            stride = std::floor(avail / wanted);
        }
        const double olo = std::max(avail - stride * wanted, 0.0);
        const double ohi = std::min(avail - stride * (wanted - 1.0), stride);
        double offset = std::fmod(std::fmod(-emin, stride) + stride, stride);
        if (!(olo <= offset && offset < ohi)) {
            offset = olo;
        }
        for (double e = emin + offset - stride; e <= emax + stride && decades.size() < 1024;
             e += stride) {
            decades.push_back(e);
        }
    }

    const auto inView = [&](double v) {
        return v >= low * (1.0 - 1e-12) && v <= high * (1.0 + 1e-12);
    };
    LogLocated located;
    located.stride = stride;
    for (const double decade : decades) {
        const double v = std::pow(base, decade);
        if (inView(v)) {
            located.majors.push_back(v);
        }
    }

    const std::vector<double> subs = avail >= 10.0 ? std::vector<double>{} : logSubs(base);
    if (!subs.empty() && (stride == 1.0 || avail <= 1.0)) {
        for (double e = emin - 1.0; e <= emax && located.minors.size() < 4096; ++e) {
            const double power = std::pow(base, e);
            for (const double sub : subs) {
                const double v = sub * power;
                if (inView(v)) {
                    located.minors.push_back(v);
                }
            }
        }
    }

    if (stride == 1.0 && located.majors.size() + located.minors.size() <= 1) {
        const double step = niceStep(high - low, target);
        std::vector<double> kept;
        for (const double v : ticksBetween(low, high, step)) {
            const bool taken = std::any_of(located.majors.begin(), located.majors.end(),
                                           [&](double m) { return std::abs(m - v) < step / 2.0; });
            if (!taken) {
                kept.push_back(v);
            }
        }
        located.minors = kept;
        located.linear = true;
    }
    return located;
}

// `LogFormatter.set_locs`: which multiples of a power are numbered on an
// axis running `low`..`high`, as the set of whole coefficients allowed.
//
// One coefficient -- the power itself -- on an axis crossing more than one
// power. Under that, a subset spaced evenly in ratio if the axis is more
// than 0.4 of a power across (1, 2, 3, 4 and 6 on base ten), and every
// whole multiple if it is less.
//
// `math.log(x, b)`, which is a division even for base ten, and not
// logOf. The difference is an ulp and it is a visible one: log(0.001)
// over log(10) is -2.9999999999999996, so matplotlib counts 0.001 to
// 0.01 as crossing one power and numbers its multiples, where 1 to 10,
// whose logarithms are exact, crosses two and is numbered at its ends.
// A reader comparing the two figures sees the ulp, so it is kept.
//
// `floor(nextafter(lmin, -inf))`: a view starting exactly on a power
// has crossed it.
std::vector<double> logSublabels(double low, double high, double base)
{
    if (!(low > 0.0) || !(high > low) || !usableBase(base)) {
        return {1.0};
    }
    const double lmin = std::log(low) / std::log(base);
    const double lmax = std::log(high) / std::log(base);
    const double below = isInteger(lmin) ? lmin - 1.0 : std::floor(lmin);
    const double crossed = std::floor(lmax) - below;
    if (crossed > 1.0) {
        return {1.0};
    }
    std::vector<double> found;
    if (lmax - lmin > 0.4) {
        const double count = std::floor(std::floor(base) / 2.0) + 1.0;
        for (double i = 0.0; i < count; ++i) {
            const double c = jsRound(std::pow(base, count > 1.0 ? i / (count - 1.0) : 0.0));
            if (std::find(found.begin(), found.end(), c) == found.end()) {
                found.push_back(c);
            }
        }
        return found;
    }
    for (double c = 1.0; c <= base; ++c) {
        found.push_back(c);
    }
    return found;
}

// How a base is written: as a whole number where it is one, `e` where it
// is e, and otherwise as the number. matplotlib writes e out to sixteen
// digits, which is the one place this does not copy it.
std::string baseText(double base)
{
    if (std::abs(base - std::numbers::e) < 1e-12) {
        return "e";
    }
    return numberText(base);
}

// A tick on a logarithmic axis, written the way `LogFormatterSciNotation`
// writes it: `10³` at a power and `2×10³` at a multiple of one -- or
// nothing, where `sublabels` says the multiple is not numbered.
//
// Returned in two parts, because the exponent is set raised and in the
// smaller face (Theme.readoutMinor), which is how mathtext sets it and
// what a reader of matplotlib's axes is used to reading. `text` is the
// whole of it in one string with the exponent in superscript digits, for
// anything that wants a single string: a test, a width, a tooltip.
//
// `precision` is the significant figures of the coefficient: six, which
// is `%g`'s, unless the ticks beside it would not be told apart by six --
// see logLabels.
std::optional<TickLabel> logLabel(double value, double base, const std::vector<double>& sublabels,
                                  int precision)
{
    const double fx = std::log(value) / std::log(base);
    const bool decade = nearInteger(fx);
    const double exponent = decade ? jsRound(fx) : std::floor(fx);
    const double coeff = jsRound(std::pow(base, fx - exponent));
    if (std::find(sublabels.begin(), sublabels.end(), coeff) == sublabels.end()) {
        return std::nullopt;
    }
    const std::string power = baseText(base);
    const std::string sign = exponent < 0.0 ? "−" : "";
    const std::string digits = numberText(std::abs(exponent));
    std::string lead;
    if (!decade) {
        double c = value / std::pow(base, exponent);
        if (nearInteger(c)) {
            c = jsRound(c);
        }
        lead = numberText(numberOf(toPrecision(c, precision))) + "×";
    }
    static const char* const kSuperscripts[] = {"⁰", "¹", "²", "³", "⁴", "⁵", "⁶", "⁷", "⁸", "⁹"};
    std::string raised = exponent < 0.0 ? "⁻" : "";
    for (const char digit : digits) {
        if (digit >= '0' && digit <= '9') {
            raised += kSuperscripts[digit - '0'];
        }
    }
    return TickLabel{lead + power, sign + digits, lead + power + raised};
}

// Every tick in `values` written, with the coefficient given as many
// figures as it takes for no two labels to read alike.
//
// `%g`'s six are what matplotlib uses and are nearly always enough; the
// case they are not is a window zoomed so far in that the linear fallback
// is stepping in the seventh figure, where matplotlib prints a column of
// identical labels. An axis whose numbers do not differ is not numbered.
std::vector<std::optional<TickLabel>> logLabels(const std::vector<double>& values, double base,
                                                const std::vector<double>& sublabels)
{
    std::vector<std::optional<TickLabel>> out;
    for (int precision = 6; precision <= 15; ++precision) {
        out.clear();
        for (const double v : values) {
            out.push_back(logLabel(v, base, sublabels, precision));
        }
        std::vector<std::string> seen;
        bool clash = false;
        for (const std::optional<TickLabel>& label : out) {
            if (!label.has_value()) {
                continue;
            }
            if (std::find(seen.begin(), seen.end(), label->text) != seen.end()) {
                clash = true;
                break;
            }
            seen.push_back(label->text);
        }
        if (!clash) {
            break;
        }
    }
    return out;
}

// The powers of `factor` between `low` and `high`, anchored at one.
//
// What a custom step means on a logarithmic axis -- see gridValues. A
// factor of one or less is the nonsense a half-typed box resolves to and
// draws nothing, exactly as a step of zero does on a linear axis.
//
// The axis's own base does not enter into it: a factor *is* a base, and
// this is the reader stating one for the grid alone. Ten gives the
// decades on any axis, and two the doublings.
std::vector<double> factorsBetween(double low, double high, double factor)
{
    std::vector<double> found;
    if (!(low > 0.0) || !(high > low) || !(factor > 1.0)) {
        return found;
    }
    const double step = std::log10(factor);
    const double to = std::log10(high);
    const double first = std::ceil(std::log10(low) / step - 1e-9);
    for (int i = 0; i < 512; ++i) {
        const double exponent = (first + i) * step;
        if (exponent > to + 1e-9) {
            break;
        }
        found.push_back(std::pow(10.0, exponent));
    }
    return found;
}

// The values an axis is numbered at, what each one says, and every value
// it carries a tick at whether numbered or not.
//
// On a linear axis those are one list and the numbers are a round step
// apart. On a logarithmic one they are matplotlib's (see logLocate): the
// ticks are its majors and its minors, and the numbered ones are the
// majors and whichever minors logSublabels allows.
//
// `logarithmic` in the answer is whether the locator placed the ticks at
// powers and their multiples -- false on a linear axis, and false on a
// logarithmic one zoomed until the linear fallback took over. The rules
// and the band's readout both follow it, so that a pane numbered in round
// linear steps is ruled and read in them too.
//
// `extent` is the length of the axis on the pane, which is what decides
// how many numbers it has room for; see logTickRoom.
TickSet tickValues(double low, double high, bool logarithmic, double base, double extent,
                   bool vertical, const TickStyle& style)
{
    TickSet set;
    if (logarithmic) {
        const LogLocated located = logLocate(
            low, high, base, logTickRoom(extent, vertical, style.fontPixels), style.target);
        std::vector<double> all = located.majors;
        all.insert(all.end(), located.minors.begin(), located.minors.end());
        std::sort(all.begin(), all.end());
        const std::vector<std::optional<TickLabel>> labels =
            logLabels(all, base, logSublabels(low, high, base));
        for (std::size_t i = 0; i < all.size(); ++i) {
            if (labels[i].has_value()) {
                set.values.push_back(all[i]);
                set.texts.push_back(*labels[i]);
            }
        }
        set.ticks = all;
        set.logarithmic = !located.linear;
        set.fallback = located.linear;
        return set;
    }
    set.values = ticksBetween(low, high, niceStep(high - low, style.target));
    for (const double v : set.values) {
        const std::string text = labelFor(v, low, high);
        set.texts.push_back(TickLabel{text, "", text});
    }
    set.ticks = set.values;
    return set;
}

// Whether `value` is one of `values`, to within what two ways of
// computing one number can differ by.
bool holds(const std::vector<double>& values, double value)
{
    for (const double v : values) {
        const double scale = std::max(std::abs(v), std::abs(value));
        if (std::abs(v - value) <= scale * 1e-9) {
            return true;
        }
    }
    return false;
}

// The values an axis is ruled at, given the reader's own step.
//
// The four densities say the same thing on either scale: `none` rules
// nowhere, `loose` rules where the numbers are, `dense` cuts the same
// axis finer, and `custom` rules where the reader says. What differs is
// only what a step is made of.
//
// On a logarithmic axis `dense` is every tick matplotlib's locator
// carries -- its majors and its minors, which is `grid(which="both")` --
// and `loose` is the numbered ones, which is `grid(which="major")` with
// the numbered multiples of an axis inside one power added, since those
// are numbers too. See logLocate for when a power has multiples at all:
// past ten powers, or once they are strided, it has none, and dense is
// the powers.
//
// `custom` is the reader's own number and is taken as given, including
// the nonsense ones: a step of zero or less draws nothing, which is what
// a box holding "-" or "1e" resolves to while it is being typed. On a
// logarithmic axis that number is read as a **factor** rather than as an
// increment, because an increment is the one thing it cannot be there: a
// rule every 1 up an axis running to a million is a million rules, and
// what a reader means by "every decade" or "every doubling" is a
// multiplication. So the rules go at its powers, anchored at one -- and a
// factor of ten is the decades, which is the answer that makes the
// control's own reading obvious.
//
// `found` is the axis's own tickValues: the grid is chosen on the scale
// the numbers were, so a pane zoomed until the locator fell back to round
// linear steps is ruled in linear steps with it.
//
// A logarithmic axis in its linear fallback may still have one
// power in view, numbered, and off the linear step. It is ruled.
std::vector<double> gridValues(GridMode mode, double low, double high, double custom,
                               const TickSet& found, const TickStyle& style)
{
    if (mode == GridMode::None) {
        return {};
    }
    if (mode == GridMode::Custom) {
        return found.logarithmic ? factorsBetween(low, high, custom)
                                 : ticksBetween(low, high, custom > 0.0 ? custom : 0.0);
    }
    if (!found.logarithmic) {
        const int target =
            mode == GridMode::Dense ? style.target * style.denseFactor : style.target;
        std::vector<double> linear = ticksBetween(low, high, niceStep(high - low, target));
        if (!found.fallback) {
            return linear;
        }
        std::vector<double> ruled = linear;
        for (const double v : found.ticks) {
            if (!holds(linear, v)) {
                ruled.push_back(v);
            }
        }
        std::sort(ruled.begin(), ruled.end());
        return ruled;
    }
    return mode == GridMode::Dense ? found.ticks : found.values;
}

// What a subdivision says: how many times the power below it the mark is
// -- 2 through 9 on base ten, the single digits log paper prints.
//
// Cleaned through toPrecision before it is written, because the value was
// built by multiplying: 3 x 0.001 over 0.001 is not 3 in doubles, and a
// grid line reading "2.9999999999999996" would be this arithmetic showing
// through the axis.
std::string minorLabel(double value, double reference)
{
    if (!(reference > 0.0)) {
        return "";
    }
    const double mantissa = numberOf(toPrecision(value / reference, 6));
    return isInteger(mantissa) ? numberText(mantissa) : numberText(numberOf(toFixed(mantissa, 3)));
}

// The power of `base` at or below `value`.
double powerBelow(double value, double base)
{
    return std::pow(base, std::floor(logOf(value, base) + 1e-9));
}

// Which of the grid's rules carry a digit, and what each one says.
//
// This application's own addition to matplotlib's axis, and off unless
// asked for. matplotlib numbers a power's multiples only on an axis
// crossing at most one power, and then in full (`2×10⁰`); past that the
// multiples are rules with nothing on them, and this puts their digit
// beside them in the smaller face. So it does nothing where matplotlib is
// already numbering them: two numbers under one rule would be saying the
// same thing twice.
//
// Taken from what is actually ruled rather than worked out a second time,
// so a digit can never appear where there is no line under it. There are
// multiples to rule only while every power is numbered (see logLocate:
// a strided axis has none), so what each one is measured against is the
// power below it, and that power is always one the reader can see.
//
// As many as there is room for, kept in order from the bottom. That order
// is what makes it the *most* there is room for rather than merely a lot:
// the candidates are fixed positions, so keeping every one that clears
// the last one kept is the largest set that can be kept at all.
//
// Each has to clear the numbered tick above it as well as whatever was
// last drawn below it. One that clears its neighbour and then collides
// with the number over it has not fitted, and the mark just under a
// numbered tick is where this happens: on base ten the 9 sits a twentieth
// of a decade below the next number where the 2 sits three tenths above
// the last.
//
// `gapPixels` is the room two labels need whatever they say, and
// `perChar` is what each character of them adds. Down the side of the
// pane a label is a line of type tall however long it is, so the first
// carries it and the second is nothing; along the bottom a label is as
// wide as it is long, and two of them clear each other by half of each.
//
// A custom grid rules at the reader's own factor. Those marks are not
// subdivisions of a power and have nothing to say about one: a factor
// of two over a decade puts rules at 2, 4 and 8 times it and then at
// 1.6 times the next, which is arithmetic rather than a reading.
//
// Every rule is numbered already, so there is nothing here to say --
// which is also what keeps `loose` out, and `none`, which rules
// nowhere at all.
//
// matplotlib is numbering the multiples itself.
//
// Where each rule is and what it would say, with the numbered ones
// carrying their own label so that a digit beside one clears the width
// of what is actually printed there.
//
// A run that can offer a single number offers none. It would be the
// same number in every run -- base three has one multiple to a power
// and would print "2" beside every one of them -- and what it says,
// that this rule is the one between, the rule says by being there.
//
// The next numbered rule above each one, taken in a pass backwards so
// that the walk forwards can look ahead without searching.
std::vector<MinorNumber> minorNumbersOf(bool enabled, GridMode mode,
                                        const std::vector<double>& values, const TickSet& found,
                                        double low, double high, double base, double extent,
                                        double gapPixels, double perChar)
{
    std::vector<MinorNumber> out;
    if (!enabled || !found.logarithmic || !(extent > 0.0)) {
        return out;
    }
    if (mode == GridMode::Custom) {
        return out;
    }
    const std::vector<double>& labels = found.values;
    if (labels.empty() || values.size() <= labels.size()) {
        return out;
    }
    const bool multiples = std::any_of(labels.begin(), labels.end(),
                                       [&](double v) { return !nearInteger(logOf(v, base)); });
    if (multiples) {
        return out;
    }

    const std::size_t n = values.size();
    std::vector<double> spots(n);
    std::vector<std::string> texts(n);
    std::vector<bool> numbered(n);
    for (std::size_t i = 0; i < n; ++i) {
        int at = -1;
        for (std::size_t j = 0; j < labels.size() && at < 0; ++j) {
            if (holds({labels[j]}, values[i])) {
                at = static_cast<int>(j);
            }
        }
        spots[i] = fractionOn(values[i], low, high, true, base) * extent;
        texts[i] = at >= 0 ? found.texts[static_cast<std::size_t>(at)].text
                           : minorLabel(values[i], powerBelow(values[i], base));
        numbered[i] = at >= 0;
    }

    int mostMarks = 0;
    int markRun = 0;
    for (std::size_t i = 0; i < n; ++i) {
        markRun = numbered[i] ? 0 : markRun + 1;
        mostMarks = std::max(mostMarks, markRun);
    }
    if (mostMarks < 2) {
        return out;
    }

    std::vector<double> above(n, kInfinity);
    std::vector<std::string> aboveText(n);
    for (std::size_t k = n; k-- > 0;) {
        if (numbered[k]) {
            above[k] = spots[k];
            aboveText[k] = texts[k];
        }
        else if (k + 1 < n) {
            above[k] = above[k + 1];
            aboveText[k] = aboveText[k + 1];
        }
    }

    const auto roomFor = [&](const std::string& one, const std::string& other) {
        return gapPixels + perChar * (textLength(one) + textLength(other)) / 2.0;
    };

    double lastAt = -kInfinity;
    std::string lastText;
    for (std::size_t i = 0; i < n; ++i) {
        if (numbered[i]) {
            lastAt = spots[i];
            lastText = texts[i];
            continue;
        }
        if (texts[i].empty()) {
            continue;
        }
        if (spots[i] - lastAt < roomFor(lastText, texts[i])) {
            continue;
        }
        if (above[i] - spots[i] < roomFor(aboveText[i], texts[i])) {
            continue;
        }
        out.push_back(MinorNumber{spots[i] / extent, texts[i]});
        lastAt = spots[i];
        lastText = texts[i];
    }
    return out;
}

// One entry per rule: where it goes, and whether it is a major.
//
// A major is a rule that lands on a numbered tick, and it is drawn a step
// stronger than the rest. The distinction only exists where the rules
// outnumber the labels: under `custom` the reader asked for exactly these
// so there is nothing to demote, and on a linear axis `loose` rules at
// the labels and has nothing either.
//
// Tested by counting rather than by naming `dense`. On a logarithmic axis
// inside one power the numbered multiples are majors too, and a dense
// grid there has more numbers than one per power to be strong at.
//
// Tested against the numbered values themselves rather than by dividing
// by their spacing, which is what this used to do and what a logarithmic
// axis has no answer for: its ticks have no one spacing to be divided by.
// The comparison is a relative one for the reason the division needed a
// tolerance -- a rule at 0.30000000000000004 and a label at 0.3 are the
// same rule, and comparing them exactly would draw the one line that
// ought to be strongest as the faintest thing on the axis.
std::vector<Rule> gridAt(GridMode mode, const std::vector<double>& values,
                         const std::vector<double>& labels, double low, double high,
                         bool logarithmic, double base)
{
    std::vector<Rule> out;
    const bool demote =
        mode != GridMode::Custom && !labels.empty() && values.size() > labels.size();
    for (const double v : values) {
        out.push_back(
            Rule{fractionOn(v, low, high, logarithmic, base), demote ? holds(labels, v) : true});
    }
    return out;
}

} // namespace plotcore
