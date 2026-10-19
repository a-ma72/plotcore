// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

// The rules the viewer held its axes to, asked of the arithmetic.
//
// The viewer's QML suite asked these of PlotFrame and PlotSurface for as long
// as those were where an axis was numbered and a window was padded: which
// ticks matplotlib places over a window, how many figures a label carries,
// when a power is cut up, what a base is. The frame is this library's now
// (Ticks, Camera), and the questions that are about the arithmetic rather than
// about a window on a screen came with it -- the viewer's suite goes on asking
// what its pane shows. Each case is the viewer's, with its reasoning, and the
// expected values are the ones it asserted; test_plotviewer holds the same
// functions to every answer the QML gave, and these hold them to why.

#include "plotcore/Camera.hpp"
#include "plotcore/Format.hpp"
#include "plotcore/Ticks.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

using namespace plotcore;
using Catch::Approx;

namespace {

/// What the viewer's frame asked with: the room of a pane a thousand pixels
/// long, in the readout face, ten numbers wanted.
TickSet ticksOver(double low, double high, bool logarithmic, double base, bool vertical)
{
    return tickValues(low, high, logarithmic, base, 1000.0, vertical);
}

std::string joined(const TickSet& found)
{
    std::string out;
    for (const TickLabel& label : found.texts) {
        out += (out.empty() ? "" : " ") + label.text;
    }
    return out;
}

/// How many figures after the point a label carries, in its mantissa.
int figures(const std::string& text)
{
    const std::string mantissa = text.substr(0, text.find('e'));
    const std::size_t point = mantissa.find('.');
    return point == std::string::npos ? 0 : static_cast<int>(mantissa.size() - point - 1);
}

std::vector<long> exponents(const LogLocated& located)
{
    std::vector<long> out;
    for (const double value : located.majors) {
        out.push_back(std::lround(std::log10(value)));
    }
    return out;
}

} // namespace

TEST_CASE("every number on an axis is written to as many figures as every other", "[axes]")
{
    // ...and the crosshair's to as many again. The count was asked of each
    // value, so it stepped up wherever a tick passed the span: an axis from
    // 2.6 to 6.6 million read "3.00e+6" beside "4.500e+6".
    const std::vector<std::pair<double, double>> windows{
        {2.6e6, 6.6e6}, {0.0, 1e7}, {-3e7, 4.5e7}, {1.00012, 1.00019}, {0.5, 950.5}};
    for (const auto& [low, high] : windows) {
        INFO(low << ".." << high);
        const TickSet found = ticksOver(low, high, false, 10.0, false);
        std::vector<int> counts;
        for (const TickLabel& label : found.texts) {
            if (label.text != "0") {
                counts.push_back(figures(label.text));
            }
        }
        INFO(joined(found));
        REQUIRE(counts.size() > 1);
        for (const int count : counts) {
            REQUIRE(count == counts.front());
        }
        const double between = (low + high) / 2.0 + (high - low) / 7.0;
        REQUIRE(figures(axisLabel(between, low, high, false)) == counts.front());
    }
}

TEST_CASE("the ticks and their labels are matplotlib's, window for window", "[axes][log]")
{
    // Each row is what matplotlib 3.11 draws on a y axis with room for nine
    // numbers over that window: how many ticks it places, and the labels on
    // the ones it numbers.
    //
    // Two rows are worth a word. 2..6 is numbered at 2, 3, 4 and 6 and not at
    // 5: past 0.4 of a decade matplotlib numbers the multiples spaced evenly
    // in ratio. And 0.001..0.01 has its multiples numbered where 1..10 does
    // not, because matplotlib's log(0.001) over log(10) is an ulp short of
    // -3 -- so it counts one power crossed there and two over 1..10. A
    // reader comparing the two figures sees the ulp, so it is kept.
    struct Row
    {
        double low;
        double high;
        double base;
        std::size_t count;
        std::string labels;
    };
    const std::vector<Row> rows{
        {1.0, 1e6, 10.0, 7 + 6 * 8,
         "10\xE2\x81\xB0 10\xC2\xB9 10\xC2\xB2 10\xC2\xB3 10\xE2\x81\xB4 10\xE2\x81\xB5 "
         "10\xE2\x81\xB6"},
        {0.5, 50.0, 10.0, 19, "10\xE2\x81\xB0 10\xC2\xB9"},
        {1.0, 10.0, 10.0, 10, "10\xE2\x81\xB0 10\xC2\xB9"},
        {2.0, 5.0, 10.0, 4,
         "2\xC3\x97"
         "10\xE2\x81\xB0 3\xC3\x97"
         "10\xE2\x81\xB0 4\xC3\x97"
         "10\xE2\x81\xB0 5\xC3\x97"
         "10\xE2\x81\xB0"},
        {2.0, 6.0, 10.0, 5,
         "2\xC3\x97"
         "10\xE2\x81\xB0 3\xC3\x97"
         "10\xE2\x81\xB0 4\xC3\x97"
         "10\xE2\x81\xB0 6\xC3\x97"
         "10\xE2\x81\xB0"},
        {0.001, 0.01, 10.0, 10,
         "10\xE2\x81\xBB\xC2\xB3 2\xC3\x97"
         "10\xE2\x81\xBB\xC2\xB3 3\xC3\x97"
         "10\xE2\x81\xBB\xC2\xB3 4\xC3\x97"
         "10\xE2\x81\xBB\xC2\xB3 6\xC3\x97"
         "10\xE2\x81\xBB\xC2\xB3 10\xE2\x81\xBB\xC2\xB2"},
        {1.0, 1e30, 10.0, 8,
         "10\xE2\x81\xB0 10\xE2\x81\xB4 10\xE2\x81\xB8 10\xC2\xB9\xC2\xB2 10\xC2\xB9\xE2\x81\xB6 "
         "10\xC2\xB2\xE2\x81\xB0 10\xC2\xB2\xE2\x81\xB4 10\xC2\xB2\xE2\x81\xB8"},
        {0.3, 70.0, 4.0, 12, "4\xE2\x81\xB0 4\xC2\xB9 4\xC2\xB2 4\xC2\xB3"},
        {1.0, 1024.0, 2.0, 6,
         "2\xE2\x81\xB0 2\xC2\xB2 2\xE2\x81\xB4 2\xE2\x81\xB6 2\xE2\x81\xB8 2\xC2\xB9\xE2\x81\xB0"},
    };
    for (const Row& row : rows) {
        INFO(row.low << ".." << row.high << " base " << row.base);
        const TickSet found = ticksOver(row.low, row.high, true, row.base, true);
        REQUIRE(found.ticks.size() == row.count);
        REQUIRE(joined(found) == row.labels);
    }
    // The multiples of 0.3..70 on base four, which is where "the power below"
    // is visibly not the power of ten below.
    const std::vector<double> multiples{0.5, 0.75, 1, 2, 3, 4, 8, 12, 16, 32, 48, 64};
    const TickSet four = ticksOver(0.3, 70.0, true, 4.0, true);
    REQUIRE(four.ticks.size() == multiples.size());
    for (std::size_t i = 0; i < multiples.size(); ++i) {
        REQUIRE(four.ticks[i] == Approx(multiples[i]).epsilon(1e-6));
    }
}

TEST_CASE("a very wide logarithmic axis strides its decades", "[axes][log]")
{
    // Nine numbers at most, so six hundred decades are numbered every
    // seventy-five of them, and thirty every four -- matplotlib's stride.
    REQUIRE(exponents(logLocate(1e-300, 1e300, 10.0, 9, 8)) ==
            std::vector<long>{-300, -225, -150, -75, 0, 75, 150, 225, 300});
    REQUIRE(exponents(logLocate(1.0, 1e30, 10.0, 9, 8)) ==
            std::vector<long>{0, 4, 8, 12, 16, 20, 24, 28});
    // A strided axis has nothing between its numbers.
    REQUIRE(logLocate(1.0, 1e30, 10.0, 9, 8).minors.empty());

    // How much room there is follows the pane: two lines of type a number up
    // the side and three characters along the bottom, never fewer than two
    // numbers and never more than nine.
    REQUIRE(logTickRoom(1000.0, true, 10.0) == 9);
    REQUIRE(logTickRoom(0.0, true, 10.0) == 2);
    REQUIRE(logTickRoom(100.0, true, 10.0) > logTickRoom(100.0, false, 10.0));

    // ...and a number out there is still short. A power of ten is round at
    // every magnitude, which is the whole difference from a linear axis's
    // round numbers.
    const auto sci = [](double value) { return logLabel(value, 10.0, {1.0}, 6)->text; };
    REQUIRE(sci(1e-300) == "10\xE2\x81\xBB\xC2\xB3\xE2\x81\xB0\xE2\x81\xB0");
    REQUIRE(sci(1e300) == "10\xC2\xB3\xE2\x81\xB0\xE2\x81\xB0");
    REQUIRE(sci(1.0) == "10\xE2\x81\xB0");
    // The band's readout is a number and not a power, and stays short too.
    REQUIRE(logLabelFor(1e6) == "1e+6");
    REQUIRE(logLabelFor(0.001) == "0.001");
    REQUIRE(logLabelFor(100.0) == "100");
}

TEST_CASE("whether a power is subdivided is matplotlib's rule", "[axes][log]")
{
    // A power is cut at the whole multiples of the power below it, and only
    // where there are at least two of them to cut it at: an octave holds
    // nothing, and neither does e, which is under three.
    REQUIRE(logSubs(2.0).empty());
    REQUIRE(logSubs(std::numbers::e).empty());
    REQUIRE(logSubs(4.0) == std::vector<double>{2.0, 3.0});
    REQUIRE(logSubs(10.0) == std::vector<double>{2, 3, 4, 5, 6, 7, 8, 9});

    // Ten powers or more, never -- however generous the base.
    REQUIRE(logLocate(1.0, 1e10, 10.0, 9, 8).minors.empty());
    REQUIRE_FALSE(logLocate(1.0, 1e8, 10.0, 9, 8).minors.empty());

    // ...and a narrower window of base four does subdivide, because what the
    // rule is about is how many powers are crossed.
    const LogLocated four = logLocate(1.0, 100.0, 4.0, 9, 8);
    REQUIRE(four.majors.size() == 4);
    REQUIRE(four.minors.size() == 6);
}

TEST_CASE("a base zoomed inside a power numbers itself linearly", "[axes][log]")
{
    // Well inside one octave: 1.1 to 1.9 holds no power of two, and an axis
    // numbered only at powers would carry no number at all -- the failure
    // this prevents. matplotlib's fallback is a linear locator over the
    // window, written as a multiple of the power it is inside.
    const LogLocated located = logLocate(1.1, 1.9, 2.0, 9, 8);
    REQUIRE(located.majors.empty());
    REQUIRE(located.linear);
    const TickSet found = tickValues(1.1, 1.9, true, 2.0, 500.0, true);
    REQUIRE_FALSE(found.logarithmic);
    REQUIRE(found.texts.size() >= 4);
    for (const TickLabel& label : found.texts) {
        INFO(label.base);
        REQUIRE(label.base.rfind("1.", 0) == 0);
        REQUIRE(label.base.size() > 4);
        REQUIRE(label.base.substr(label.base.size() - 3) == "\xC3\x97"
                                                            "2");
        REQUIRE(label.exponent == "0");
    }
}

TEST_CASE("a logarithmic axis takes its air in decades", "[axes][log]")
{
    // A twentieth of the span at each end, measured in the axis's own scale.
    const Span wide = padded(1.0, 1000.0, true, 10.0);
    REQUIRE(std::log10(wide.low) == Approx(-0.15).margin(1e-9));
    REQUIRE(std::log10(wide.high) == Approx(3.15).margin(1e-9));

    // A flat line has no span to take a share of, and is widened to the
    // powers either side of it before the air goes on -- matplotlib's
    // `LogLocator.nonsingular`, which draws a line at 7 between 10^-0.05 and
    // 10^1.05, and one at 10 between 10^-0.1 and 10^2.1.
    const Span flat = padded(7.0, 7.0, true, 10.0);
    REQUIRE(std::log10(flat.low) == Approx(-0.05).margin(1e-9));
    REQUIRE(std::log10(flat.high) == Approx(1.05).margin(1e-9));
    const Span onPower = padded(10.0, 10.0, true, 10.0);
    REQUIRE(std::log10(onPower.low) == Approx(-0.1).margin(1e-9));
    REQUIRE(std::log10(onPower.high) == Approx(2.1).margin(1e-9));

    // Nothing above zero is one empty decade rather than a span of none: a
    // frame with numbers on it beats a frame with none.
    const Span nothing = padded(0.0, -5.0, true, 10.0);
    REQUIRE(nothing.low == 1.0);
    REQUIRE(nothing.high == 10.0);

    // ...and the linear rule is untouched by any of it.
    const Span linear = padded(0.0, 100.0, false, 10.0);
    REQUIRE(linear.low == -5.0);
    REQUIRE(linear.high == 105.0);
    const Span flatLinear = padded(7.0, 7.0, false, 10.0);
    REQUIRE(flatLinear.low == 6.0);
    REQUIRE(flatLinear.high == 8.0);
}

TEST_CASE("only a number above one is a base", "[axes][log]")
{
    REQUIRE(usableBase(10.0));
    REQUIRE(usableBase(2.0));
    REQUIRE(usableBase(std::numbers::e));
    // Legal, however little use it is.
    REQUIRE(usableBase(1.0000001));
    REQUIRE_FALSE(usableBase(1.0));
    REQUIRE_FALSE(usableBase(0.0));
    REQUIRE_FALSE(usableBase(-2.0));
    REQUIRE_FALSE(usableBase(0.5));
    REQUIRE_FALSE(usableBase(std::nan("")));
    REQUIRE_FALSE(usableBase(std::numeric_limits<double>::infinity()));

    // ...and nothing downstream is ever handed one that is not. A stated base
    // that is not a base falls back to ten rather than to a pane that has
    // quietly stopped drawing.
    REQUIRE(baseFor("custom", 1.0) == 10.0);
    REQUIRE(baseFor("custom", -3.0) == 10.0);
    REQUIRE(baseFor("custom", std::nan("")) == 10.0);
    REQUIRE(baseFor("custom", 7.0) == 7.0);
    REQUIRE(baseFor("2", 7.0) == 2.0);
    REQUIRE(baseFor("e", 7.0) == Approx(std::numbers::e).margin(1e-12));
    REQUIRE(baseFor("10", 7.0) == 10.0);
    // A mode from some later version costs the reader that base and not the
    // whole of their view, which is the stance every remembered setting is
    // read under.
    REQUIRE(baseFor("tau", 7.0) == 10.0);
}
