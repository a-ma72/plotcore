// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-FileCopyrightText: 2026 Andreas Martin
// SPDX-License-Identifier: GPL-3.0-only

// The whole plot, driven as a host drives it: lines in, gestures in, a Scene
// out -- with no window. What the store holds is checked against the elements
// it was handed, and what a gesture does is checked in the pixels it moved,
// because a host sees nothing else.

#include "plotcore/Figure.hpp"
#include "plotcore/Plot.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>
#include <vector>

using namespace plotcore;
using Catch::Approx;

namespace {

/// A random walk with a one-sample spike every `every` elements, so an
/// envelope that loses a sample or moves one has something to lose.
std::vector<double> walk(long long count, long long every = 997)
{
    std::vector<double> out(static_cast<std::size_t>(count));
    unsigned state = 12345;
    double at = 0.0;
    for (long long i = 0; i < count; ++i) {
        state = state * 1103515245u + 12345u;
        at += (static_cast<double>((state >> 16) & 0x7fff) / 32767.0 - 0.5);
        out[static_cast<std::size_t>(i)] = (i % every == every - 1) ? at + 500.0 : at;
    }
    return out;
}

std::vector<double> ramp(long long count, double from, double step)
{
    std::vector<double> out(static_cast<std::size_t>(count));
    for (long long i = 0; i < count; ++i) {
        out[static_cast<std::size_t>(i)] = from + static_cast<double>(i) * step;
    }
    return out;
}

/// Every value drawn is the element it names, at that element.
void requireElements(const PlotLine& line, const std::vector<double>& values)
{
    REQUIRE(line.count > 0);
    for (long long i = 0; i < line.count; ++i) {
        const double value = line.values[i];
        if (std::isnan(value)) {
            continue;
        }
        const double at = line.positions != nullptr
                              ? line.positions[i]
                              : line.positionStart + static_cast<double>(i) * line.positionStep;
        REQUIRE(at == std::floor(at));
        REQUIRE(at >= 0.0);
        REQUIRE(at < static_cast<double>(values.size()));
        REQUIRE(value == values[static_cast<std::size_t>(at)]);
    }
}

Figure figureOf(std::vector<std::vector<double>> lines, double width = 800.0, double height = 500.0)
{
    Figure figure;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        figure.plot().addLine(lines[i].data(), static_cast<long long>(lines[i].size()),
                              "line " + std::to_string(i));
    }
    figure.setSize(width, height);
    FigureSettings settings = figure.settings();
    settings.legend = false;
    figure.setSettings(settings);
    return figure;
}

const DrawnLine& drawnOf(const Scene& scene, int line)
{
    for (const DrawnLine& drawn : scene.lines) {
        if (drawn.line == line) {
            return drawn;
        }
    }
    FAIL("line " << line << " was not drawn");
    return scene.lines.front();
}

/// The pixel sample `i` of a line short enough to be drawn as its samples.
Point sampleAt(const Scene& scene, int line, std::size_t i)
{
    const DrawnLine& drawn = drawnOf(scene, line);
    REQUIRE(drawn.runs.size() == 1);
    REQUIRE(i < drawn.runs.front().size());
    return drawn.runs.front()[i];
}

} // namespace

// --- the store ------------------------------------------------------------------

TEST_CASE("a line copied in is drawn as its own elements, at every zoom", "[plot]")
{
    const std::vector<double> values = walk(200000);
    Plot plot;
    const int line = plot.addLine(values.data(), static_cast<long long>(values.size()), "walk");
    REQUIRE(plot.baseBucket(line) == 1);
    plot.setPaneColumns(1024);

    SECTION("the whole line is its summary")
    {
        plot.setView(0.0, static_cast<double>(values.size() - 1), false);
        const PlotLine drawn = plot.drawLine(line);
        REQUIRE(drawn.summarised);
        REQUIRE(drawn.count <= 4 * 1024);
        requireElements(drawn, values);
    }
    SECTION("a window is folded finer, and still holds only elements")
    {
        for (const double span : {100000.0, 10000.0, 1000.0, 100.0}) {
            plot.setView(50000.0, 50000.0 + span, false);
            const PlotLine drawn = plot.drawLine(line);
            requireElements(drawn, values);
            const double first =
                drawn.positions != nullptr ? drawn.positions[0] : drawn.positionStart;
            REQUIRE(first <= 50000.0);
        }
    }
    SECTION("a window under a pane's width is the elements themselves")
    {
        plot.setView(1234.0, 1734.0, false);
        const PlotLine drawn = plot.drawLine(line);
        REQUIRE_FALSE(drawn.summarised);
        requireElements(drawn, values);
    }
    SECTION("its extent is the extent of its elements")
    {
        const LineExtent extent = plot.extent(line);
        REQUIRE(extent.finite);
        REQUIRE(extent.minimum == *std::min_element(values.begin(), values.end()));
        REQUIRE(extent.maximum == *std::max_element(values.begin(), values.end()));
    }
}

TEST_CASE("a line streamed in pieces is the line copied in whole", "[plot]")
{
    const std::vector<double> values = walk(300001);
    Plot plot;
    const int whole = plot.addLine(values.data(), static_cast<long long>(values.size()));
    const int streamed = plot.beginLine(static_cast<long long>(values.size()));
    REQUIRE_FALSE(plot.ready(streamed));
    for (std::size_t at = 0; at < values.size(); at += 7777) {
        const std::size_t count = std::min<std::size_t>(7777, values.size() - at);
        plot.appendToLine(streamed, values.data() + at, static_cast<long long>(count));
    }
    plot.finishLine(streamed);
    REQUIRE(plot.ready(streamed));
    plot.setPaneColumns(512);
    for (const auto& [low, high] :
         std::vector<std::pair<double, double>>{{0.0, 300000.0}, {1000.0, 9000.0}, {5.0, 70.0}}) {
        plot.setView(low, high, false);
        const PlotLine a = plot.drawLine(whole);
        const PlotLine b = plot.drawLine(streamed);
        REQUIRE(a.count == b.count);
        for (long long i = 0; i < a.count; ++i) {
            REQUIRE(a.values[i] == b.values[i]);
        }
        requireElements(b, values);
    }
}

TEST_CASE("a line over its budget is held coarser, and only a reader reaches below", "[plot]")
{
    const std::vector<double> values = walk(1 << 20);
    Plot plot;
    plot.setBudgetBytes(1 << 20); // a megabyte for a line of eight
    const int line = plot.addLine(values.data(), static_cast<long long>(values.size()));
    REQUIRE(plot.baseBucket(line) > 1);
    REQUIRE(plot.heldDoubles() <= plot.budgetDoubles());

    long long reads = 0;
    plot.setLineReader(line, [&](long long first, long long count, double* out) {
        ++reads;
        std::copy_n(values.data() + first, count, out);
        return true;
    });
    plot.setPaneColumns(256);

    // Above the base: a fold of what is held, no read.
    plot.setView(0.0, static_cast<double>(values.size()), false);
    requireElements(plot.drawLine(line), values);
    REQUIRE(reads == 0);

    // Under it: the elements, read.
    plot.setView(4000.0, 4100.0, false);
    const PlotLine closer = plot.drawLine(line);
    REQUIRE(reads > 0);
    requireElements(closer, values);
}

TEST_CASE("a time base that only goes one way narrows a closer look", "[plot]")
{
    const std::vector<double> values = walk(100000);
    std::vector<double> time(values.size());
    for (std::size_t i = 0; i < time.size(); ++i) {
        time[i] = 0.001 * static_cast<double>(i) + 1e-9 * static_cast<double>(i * i);
    }
    Plot plot;
    const int line = plot.addLine(values.data(), static_cast<long long>(values.size()));
    plot.setTime(time.data(), static_cast<long long>(time.size()));
    REQUIRE(plot.hasTime(line));
    REQUIRE(plot.xExtent(line).low == time.front());
    REQUIRE(plot.xExtent(line).high == time.back());
    plot.setPaneColumns(512);
    plot.setView(time[40000], time[40300], false);
    const PlotLine drawn = plot.drawLine(line);
    REQUIRE_FALSE(drawn.summarised);
    requireElements(drawn, values);
    // A closer look is a paneful of buckets around the window, not the line.
    REQUIRE(drawn.count <= 4 * 512);
    const PlotAxis axis = plot.drawAxis(line);
    REQUIRE(axis.values == plot.drawAxis(line).values);
    REQUIRE(axis.count == static_cast<long long>(time.size()));
}

TEST_CASE("a line's own time beats the time every line shares", "[plot]")
{
    const std::vector<double> a = ramp(1000, 0.0, 1.0);
    const std::vector<double> b = ramp(500, 0.0, 1.0);
    const std::vector<double> shared = ramp(1000, 0.0, 0.5);
    const std::vector<double> own = ramp(500, 100.0, 2.0);
    Plot plot;
    const int first = plot.addLine(a.data(), 1000);
    const int second = plot.addLine(b.data(), 500);
    plot.setTime(shared.data(), 1000);
    plot.setLineTime(second, own.data(), 500);
    REQUIRE(plot.xExtent(first).high == 499.5);
    REQUIRE(plot.xExtent(second).low == 100.0);
    REQUIRE(plot.xExtent(second).high == 1098.0);
}

TEST_CASE("a logarithmic x is folded per column and places each point", "[plot]")
{
    const std::vector<double> values = walk(1000000);
    Plot plot;
    plot.setIndexAxis(1.0, 1.0);
    const int line = plot.addLine(values.data(), static_cast<long long>(values.size()));
    REQUIRE(plot.xPositiveMinimum(line) == 1.0);
    plot.setPaneColumns(1024);
    plot.setView(1.0, 1e6, true);
    const PlotLine drawn = plot.drawLine(line);
    REQUIRE(drawn.xs != nullptr);
    // The first columns hold a sample or two each, at their own x.
    REQUIRE(drawn.xs[0] == 1.0);
    REQUIRE(drawn.values[0] == values[0]);
    for (long long i = 1; i < drawn.count; ++i) {
        REQUIRE(drawn.xs[i] >= drawn.xs[i - 1]);
    }
}

// --- the figure ------------------------------------------------------------------

TEST_CASE("a figure draws its lines inside the pane, numbered on both axes", "[figure]")
{
    std::vector<double> sine(2000);
    for (std::size_t i = 0; i < sine.size(); ++i) {
        sine[i] = std::sin(static_cast<double>(i) / 100.0);
    }
    Figure figure = figureOf({sine});
    const Scene scene = figure.scene();
    REQUIRE(scene.message.empty());
    REQUIRE(scene.lines.size() == 1);
    const PaneRect& a = scene.area;
    REQUIRE(a.x > 0.0);
    REQUIRE(a.x + a.width < 800.0);
    REQUIRE(a.y + a.height < 500.0);
    for (const auto& run : scene.lines.front().runs) {
        for (const Point& p : run) {
            REQUIRE(p.x() >= a.x - 1e-9);
            REQUIRE(p.x() <= a.x + a.width + 1e-9);
            REQUIRE(p.y() >= a.y - 1e-9);
            REQUIRE(p.y() <= a.y + a.height + 1e-9);
        }
    }
    REQUIRE_FALSE(scene.lines.front().triangles.empty());
    REQUIRE(scene.lines.front().triangles.size() % 3 == 0);

    int left = 0;
    int foot = 0;
    for (const TextItem& text : scene.texts) {
        if (text.at.x() < a.x) {
            ++left;
        }
        if (text.at.y() > a.y + a.height) {
            ++foot;
        }
    }
    REQUIRE(left >= 3);
    REQUIRE(foot >= 3);
    REQUIRE_FALSE(scene.rules.empty());
}

TEST_CASE("the wheel holds the value under the pointer and a double click puts it back", "[figure]")
{
    Figure figure = figureOf({walk(100000)});
    const Scene before = figure.scene();
    const PaneRect a = before.area;
    const double px = a.x + 0.3 * a.width;
    const double py = a.y + 0.6 * a.height;
    const double x = figure.camera().dataXAt(px, py);
    const double y = figure.camera().dataYAt(py, px);

    figure.wheel(px, py, 3 * 120.0, {});
    const Scene zoomed = figure.scene();
    REQUIRE(zoomed.x.high - zoomed.x.low < before.x.high - before.x.low);
    REQUIRE(zoomed.y.high - zoomed.y.low < before.y.high - before.y.low);
    REQUIRE(figure.camera().dataXAt(px, py) == Approx(x));
    REQUIRE(figure.camera().dataYAt(py, px) == Approx(y));

    SECTION("Shift zooms x alone, Control y alone")
    {
        figure.resetView();
        (void)figure.scene();
        figure.wheel(px, py, 120.0, Modifiers{true, false, false});
        Scene shifted = figure.scene();
        REQUIRE(shifted.y.low == before.y.low);
        REQUIRE(shifted.x.high - shifted.x.low < before.x.high - before.x.low);
        figure.resetView();
        figure.wheel(px, py, 120.0, Modifiers{false, true, false});
        shifted = figure.scene();
        REQUIRE(shifted.x.low == before.x.low);
        REQUIRE(shifted.y.high - shifted.y.low < before.y.high - before.y.low);
    }
    SECTION("a double click is the whole of it again")
    {
        figure.doubleClick(px, py, Button::Left);
        const Scene again = figure.scene();
        REQUIRE(again.x.low == before.x.low);
        REQUIRE(again.x.high == before.x.high);
        REQUIRE(again.y.low == before.y.low);
        REQUIRE(again.y.high == before.y.high);
    }
}

TEST_CASE("a drag pans, and may leave the data but not lose it", "[figure]")
{
    Figure figure = figureOf({ramp(1000, 0.0, 1.0)});
    const Scene before = figure.scene();
    const PaneRect a = before.area;
    const double cx = a.x + a.width / 2.0;
    const double cy = a.y + a.height / 2.0;
    figure.press(cx, cy, Button::Left, {});
    figure.move(cx + 50.0, cy, {});
    figure.release(cx + 50.0, cy, Button::Left, {});
    const Scene moved = figure.scene();
    const double span = before.x.high - before.x.low;
    REQUIRE(moved.x.low == Approx(before.x.low - 50.0 / a.width * span));

    // Dragged far right: a quarter of the window stays over the data.
    figure.press(cx, cy, Button::Left, {});
    figure.move(cx + 10.0 * a.width, cy, {});
    figure.release(cx + 10.0 * a.width, cy, Button::Left, {});
    const Scene far = figure.scene();
    REQUIRE(far.x.high == Approx(before.x.low + 0.25 * span));
}

TEST_CASE("a right drag zooms to the band it drew, and a slip is not a band", "[figure]")
{
    Figure figure = figureOf({walk(50000)});
    const Scene before = figure.scene();
    const PaneRect a = before.area;
    const double x0 = a.x + 0.2 * a.width;
    const double x1 = a.x + 0.5 * a.width;
    const double y0 = a.y + 0.25 * a.height;
    const double y1 = a.y + 0.75 * a.height;
    const double lowX = figure.camera().dataXAt(x0, y0);
    const double highX = figure.camera().dataXAt(x1, y0);
    figure.press(x0, y0, Button::Right, {});
    figure.move(x1, y1, {});
    const Scene drawing = figure.scene();
    REQUIRE(drawing.band.has_value());
    REQUIRE(drawing.band->width == Approx(x1 - x0));
    figure.release(x1, y1, Button::Right, {});
    const Scene after = figure.scene();
    REQUIRE_FALSE(after.band.has_value());
    REQUIRE(after.x.low == Approx(lowX));
    REQUIRE(after.x.high == Approx(highX));

    figure.press(x0, y0, Button::Right, {});
    figure.release(x0 + 3.0, y0 + 3.0, Button::Right, {});
    REQUIRE(figure.scene().x.low == after.x.low);
}

TEST_CASE("a scaled line keeps the sample under the pointer through a wheel, a pan and a band",
          "[figure]")
{
    std::vector<double> sine(400);
    for (std::size_t i = 0; i < sine.size(); ++i) {
        sine[i] = 1000.0 * std::sin(static_cast<double>(i) / 30.0) + 5000.0;
    }
    Figure figure = figureOf({ramp(400, 0.0, 1.0), sine});
    const Scene first = figure.scene();
    const Point held = sampleAt(first, 1, 200);

    // Alt and Control scale the line under the pointer, and only it.
    figure.wheel(held.x(), held.y(), 2 * 120.0, Modifiers{false, true, true});
    const Scene scaled = figure.scene();
    REQUIRE(figure.lineSettings(1).scale > 1.0);
    REQUIRE(figure.lineSettings(0).scale == 1.0);
    REQUIRE(sampleAt(scaled, 1, 200).y() == Approx(held.y()).margin(1e-6));
    REQUIRE(std::abs(sampleAt(scaled, 1, 150).y() - held.y()) >
            std::abs(sampleAt(first, 1, 150).y() - held.y()));
    REQUIRE(sampleAt(scaled, 0, 100).y() == Approx(sampleAt(first, 0, 100).y()));
    REQUIRE(scaled.x.low == first.x.low);

    // A pan moves every line by the pixels it moved.
    const PaneRect a = scaled.area;
    figure.press(a.x + 100.0, a.y + 100.0, Button::Left, {});
    figure.move(a.x + 100.0, a.y + 130.0, {});
    figure.release(a.x + 100.0, a.y + 130.0, Button::Left, {});
    const Scene panned = figure.scene();
    REQUIRE(sampleAt(panned, 1, 200).y() == Approx(held.y() + 30.0));
    REQUIRE(sampleAt(panned, 0, 100).y() == Approx(sampleAt(scaled, 0, 100).y() + 30.0));

    // A band maps itself onto the pane for every line.
    const double top = a.y + 0.2 * a.height;
    const double bottom = a.y + 0.8 * a.height;
    const Point one = sampleAt(panned, 1, 200);
    const Point zero = sampleAt(panned, 0, 200);
    figure.press(a.x + 0.1 * a.width, top, Button::Right, {});
    figure.release(a.x + 0.9 * a.width, bottom, Button::Right, {});
    const Scene banded = figure.scene();
    const auto mapped = [&](double y) { return a.y + (y - top) / (bottom - top) * a.height; };
    REQUIRE(sampleAt(banded, 1, 0).x() < a.x); // drawn off the left, clipped by the host
    REQUIRE(drawnOf(banded, 1).runs.front().size() > 0);
    // Find sample 200 again: the run now starts where the window does.
    const auto findSample = [&](const Scene& scene, int line, double x) {
        const auto& run = drawnOf(scene, line).runs.front();
        return *std::min_element(run.begin(), run.end(), [&](const Point& p, const Point& q) {
            return std::abs(p.x() - x) < std::abs(q.x() - x);
        });
    };
    const double x200 = a.x + (one.x() - (a.x + 0.1 * a.width)) / (0.8 * a.width) * a.width;
    REQUIRE(findSample(banded, 1, x200).y() == Approx(mapped(one.y())).margin(0.5));
    REQUIRE(findSample(banded, 0, x200).y() == Approx(mapped(zero.y())).margin(0.5));
}

TEST_CASE("Alt and a drag move one line, and Control and a click put it back", "[figure]")
{
    Figure figure = figureOf({ramp(300, 0.0, 1.0), ramp(300, 300.0, -1.0)});
    const Scene first = figure.scene();
    const Point at = sampleAt(first, 1, 50);
    figure.press(at.x(), at.y(), Button::Left, Modifiers{false, false, true});
    figure.move(at.x(), at.y() + 40.0, {});
    figure.release(at.x(), at.y() + 40.0, Button::Left, {});
    const Scene moved = figure.scene();
    REQUIRE(sampleAt(moved, 1, 50).y() == Approx(at.y() + 40.0));
    REQUIRE(sampleAt(moved, 0, 50).y() == Approx(sampleAt(first, 0, 50).y()));
    REQUIRE(figure.lineSettings(1).shift != 0.0);
    // ...and the pane did not move under the drag: a moved line is numbered on
    // the common axis, not beside it.
    REQUIRE(moved.area.x == first.area.x);

    const Point now = sampleAt(moved, 1, 50);
    figure.press(now.x(), now.y(), Button::Left, Modifiers{false, true, false});
    figure.release(now.x(), now.y(), Button::Left, {});
    REQUIRE(figure.lineSettings(1).shift == 0.0);
    REQUIRE(figure.lineSettings(1).scale == 1.0);
    const Scene back = figure.scene();
    REQUIRE(sampleAt(back, 1, 50).y() == Approx(at.y()));
    REQUIRE(back.area.x == first.area.x);

    SECTION("the middle button moves a line without Alt")
    {
        figure.press(at.x(), at.y(), Button::Middle, {});
        figure.move(at.x(), at.y() - 25.0, {});
        figure.release(at.x(), at.y() - 25.0, Button::Middle, {});
        REQUIRE(sampleAt(figure.scene(), 1, 50).y() == Approx(at.y() - 25.0));
    }
}

TEST_CASE("an axis of its own waits for a second line", "[figure]")
{
    const std::vector<double> a = ramp(100, 0.0, 1.0);
    const std::vector<double> b = ramp(100, 1e6, 1e3);
    Figure figure = figureOf({a});
    LineSettings own;
    own.ownAxis = true;
    own.axisLabel = "volts";
    figure.setLineSettings(0, own);
    const Scene alone = figure.scene();
    figure.plot().addLine(b.data(), 100);
    figure.setLineSettings(1, own);
    figure.setLineSettings(0, LineSettings{});
    const Scene beside = figure.scene();
    REQUIRE(beside.area.x > alone.area.x);
    // The common axis is the first line's alone, and the second spans the pane
    // on its own.
    REQUIRE(beside.y.high < 1000.0);
    const auto& run = drawnOf(beside, 1).runs.front();
    REQUIRE(run.front().y() > beside.area.y + 0.5 * beside.area.height);
    REQUIRE(run.back().y() < beside.area.y + 0.5 * beside.area.height);
    bool named = false;
    for (const TextItem& text : beside.texts) {
        named = named || (text.text == "volts" && text.vertical);
    }
    REQUIRE(named);
}

TEST_CASE("stacked lines each fill a band of the pane, the first at the top", "[figure]")
{
    Figure figure = figureOf({walk(500), ramp(500, -3.0, 0.01), ramp(500, 1e4, 1.0)});
    FigureSettings settings = figure.settings();
    settings.stacked = true;
    figure.setSettings(settings);
    const Scene scene = figure.scene();
    const PaneRect& a = scene.area;
    for (int line = 0; line < 3; ++line) {
        const double top = a.y + line * a.height / 3.0;
        const double bottom = a.y + (line + 1) * a.height / 3.0;
        double highest = 1e300;
        double lowest = -1e300;
        for (const Point& p : drawnOf(scene, line).runs.front()) {
            REQUIRE(p.y() >= top - 1e-6);
            REQUIRE(p.y() <= bottom + 1e-6);
            highest = std::min(highest, p.y());
            lowest = std::max(lowest, p.y());
        }
        // ...and fills most of it.
        REQUIRE(lowest - highest > 0.8 * a.height / 3.0);
    }
}

TEST_CASE("a flipped figure draws x up the pane and y across it", "[figure]")
{
    Figure figure = figureOf({ramp(200, 0.0, 2.0)});
    FigureSettings settings = figure.settings();
    settings.flipped = true;
    figure.setSettings(settings);
    const Scene scene = figure.scene();
    const auto& run = drawnOf(scene, 0).runs.front();
    REQUIRE(run.front().y() > run.back().y());
    REQUIRE(run.front().x() < run.back().x());
    // The modifiers name the pane's directions: Shift is across it, which is
    // now y, and Control is up it, which is now x.
    const PaneRect& a = scene.area;
    figure.wheel(a.x + a.width / 2.0, a.y + a.height / 2.0, 120.0, Modifiers{true, false, false});
    Scene zoomed = figure.scene();
    REQUIRE(zoomed.y.high - zoomed.y.low < scene.y.high - scene.y.low);
    REQUIRE(zoomed.x.low == scene.x.low);
    figure.resetView();
    figure.wheel(a.x + a.width / 2.0, a.y + a.height / 2.0, 120.0, Modifiers{false, true, false});
    zoomed = figure.scene();
    REQUIRE(zoomed.x.high - zoomed.x.low < scene.x.high - scene.x.low);
    REQUIRE(zoomed.y.low == scene.y.low);
}

TEST_CASE("the crosshair reads the sample under the pointer, written as the axis writes",
          "[figure]")
{
    const std::vector<double> values = ramp(300, 10.0, 0.5);
    Figure figure = figureOf({values});
    const Scene first = figure.scene();
    const Point at = sampleAt(first, 0, 120);
    figure.hover(at.x() + 1.0, at.y() - 1.0);
    const Scene read = figure.scene();
    REQUIRE(read.reading.valid);
    REQUIRE(read.reading.line == 0);
    REQUIRE(read.reading.x == 120.0);
    REQUIRE(read.reading.y == values[120]);
    REQUIRE(read.reading.at.x() == Approx(at.x()));
    REQUIRE_FALSE(read.reading.xText.empty());
    REQUIRE_FALSE(read.reading.yText.empty());
    figure.leave();
    REQUIRE_FALSE(figure.scene().reading.valid);

    FigureSettings settings = figure.settings();
    settings.cursor = false;
    figure.setSettings(settings);
    figure.hover(at.x(), at.y());
    REQUIRE_FALSE(figure.scene().reading.valid);
}

TEST_CASE("markers are drawn only where a line is its samples", "[figure]")
{
    Figure figure = figureOf({ramp(100, 0.0, 1.0), walk(100000)});
    FigureSettings settings = figure.settings();
    settings.markers = true;
    figure.setSettings(settings);
    const Scene scene = figure.scene();
    REQUIRE(drawnOf(scene, 0).markers.size() == 100);
    REQUIRE(drawnOf(scene, 1).markers.empty());
}

TEST_CASE("a logarithmic axis with nothing above zero says so and takes no gestures", "[figure]")
{
    Figure figure = figureOf({ramp(100, -100.0, 0.5)});
    FigureSettings settings = figure.settings();
    settings.yLog = true;
    figure.setSettings(settings);
    const Scene scene = figure.scene();
    REQUIRE_FALSE(scene.message.empty());
    const PaneRect& a = scene.area;
    figure.wheel(a.x + 10.0, a.y + 10.0, 600.0, {});
    REQUIRE(figure.scene().x.low == scene.x.low);
    REQUIRE(figure.scene().x.high == scene.x.high);
}

TEST_CASE("the legend sits in the corner asked for, inside the pane", "[figure]")
{
    Figure figure = figureOf({ramp(10, 0.0, 1.0), ramp(10, 1.0, 1.0)});
    FigureSettings settings = figure.settings();
    settings.legend = true;
    for (const Corner corner :
         {Corner::TopLeft, Corner::TopRight, Corner::BottomLeft, Corner::BottomRight}) {
        settings.legendCorner = corner;
        figure.setSettings(settings);
        const Scene scene = figure.scene();
        REQUIRE(scene.legend.size() == 2);
        REQUIRE(scene.legend[1].name == "line 1");
        const PaneRect& a = scene.area;
        const PaneRect& r = scene.legendRect;
        REQUIRE(r.x >= a.x);
        REQUIRE(r.y >= a.y);
        REQUIRE(r.x + r.width <= a.x + a.width);
        REQUIRE(r.y + r.height <= a.y + a.height);
        const bool left = corner == Corner::TopLeft || corner == Corner::BottomLeft;
        const bool top = corner == Corner::TopLeft || corner == Corner::TopRight;
        REQUIRE((r.x + r.width / 2.0 < a.x + a.width / 2.0) == left);
        REQUIRE((r.y + r.height / 2.0 < a.y + a.height / 2.0) == top);
    }
}

TEST_CASE("a view written down reads back as the same picture", "[figure]")
{
    const std::vector<double> a = walk(20000);
    const std::vector<double> b = ramp(20000, 1.0, 0.25);
    Figure figure = figureOf({a, b});
    FigureSettings settings = figure.settings();
    settings.grid = GridMode::Dense;
    settings.title = "a \"quoted\" title";
    settings.xLabel = "time";
    settings.minorNumbers = true;
    settings.colours.mode = "tol bright";
    settings.legendCorner = Corner::BottomLeft;
    figure.setSettings(settings);
    LineSettings line;
    line.colour = Rgba::of(0.25, 0.5, 0.75);
    line.ownAxis = true;
    line.axisLabel = "volts";
    figure.setLineSettings(1, line);
    const Scene before = figure.scene();
    const PaneRect& area = before.area;
    figure.wheel(area.x + 0.4 * area.width, area.y + 0.5 * area.height, 360.0, {});
    const Scene zoomed = figure.scene();
    const std::string state = figure.state();

    Figure again = figureOf({a, b});
    REQUIRE(again.restore(state));
    const Scene restored = again.scene();
    REQUIRE(restored.x.low == zoomed.x.low);
    REQUIRE(restored.x.high == zoomed.x.high);
    REQUIRE(restored.y.low == zoomed.y.low);
    REQUIRE(restored.y.high == zoomed.y.high);
    REQUIRE(again.settings().title == settings.title);
    REQUIRE(again.settings().grid == GridMode::Dense);
    REQUIRE(again.settings().colours.mode == "tol bright");
    REQUIRE(again.settings().legendCorner == Corner::BottomLeft);
    REQUIRE(again.lineSettings(1).axisLabel == "volts");
    REQUIRE(again.lineSettings(1).colour.has_value());
    REQUIRE(restored.texts.size() == zoomed.texts.size());
    for (std::size_t i = 0; i < restored.texts.size(); ++i) {
        REQUIRE(restored.texts[i].text == zoomed.texts[i].text);
    }
    REQUIRE(restored.lines.size() == zoomed.lines.size());
    for (std::size_t i = 0; i < restored.lines.size(); ++i) {
        REQUIRE(restored.lines[i].runs == zoomed.lines[i].runs);
    }
    REQUIRE(again.state() == state);

    SECTION("what is not a view is refused, and a setting it cannot read costs only that")
    {
        Figure other = figureOf({a});
        REQUIRE_FALSE(other.restore("not json"));
        REQUIRE_FALSE(other.restore("{\"grid\":\"dense\"}"));
        REQUIRE(other.settings().grid == GridMode::Loose);
        REQUIRE(other.restore("{\"plotcore\":1,\"grid\":7,\"title\":\"kept\"}"));
        REQUIRE(other.settings().grid == GridMode::Loose);
        REQUIRE(other.settings().title == "kept");
    }
}

TEST_CASE("changing a scale puts the view back, and changing a title does not", "[figure]")
{
    Figure figure = figureOf({ramp(1000, 1.0, 1.0)});
    const Scene scene = figure.scene();
    const PaneRect& a = scene.area;
    figure.wheel(a.x + a.width / 2.0, a.y + a.height / 2.0, 240.0, {});
    REQUIRE(figure.camera().zoomed());
    FigureSettings settings = figure.settings();
    settings.title = "kept";
    figure.setSettings(settings);
    REQUIRE(figure.camera().zoomed());
    settings.xLog = true;
    figure.setSettings(settings);
    REQUIRE_FALSE(figure.camera().zoomed());
}

TEST_CASE("a colour nobody chose follows the ground, and one somebody chose stays", "[figure]")
{
    Figure figure = figureOf({ramp(10, 0.0, 1.0)});
    FigureSettings settings = figure.settings();
    REQUIRE(settings.dark);
    REQUIRE_FALSE(settings.colours.custom);
    settings.colours.mode = "same";
    // Held as values, as a saved view or a host's copy holds them, and not
    // chosen: the ground's are drawn.
    settings.colours.single = LineColours::defaults(true).single;
    figure.setSettings(settings);
    REQUIRE(figure.scene().lines.front().colour == LineColours::defaults(true).single);
    settings.dark = false;
    figure.setSettings(settings);
    REQUIRE(figure.scene().lines.front().colour == LineColours::defaults(false).single);

    settings.colours.single = Rgba::of(0.2, 0.4, 0.6);
    settings.colours.custom = true;
    figure.setSettings(settings);
    REQUIRE(figure.scene().lines.front().colour == Rgba::of(0.2, 0.4, 0.6));
    settings.dark = true;
    figure.setSettings(settings);
    REQUIRE(figure.scene().lines.front().colour == Rgba::of(0.2, 0.4, 0.6));

    SECTION("a view written before the flag says by its colours whether they were chosen")
    {
        Figure old = figureOf({ramp(10, 0.0, 1.0)});
        REQUIRE(old.restore(R"({"plotcore":1,"colours":{"mode":"same","single":"#ffffff"}})"));
        REQUIRE_FALSE(old.settings().colours.custom);
        REQUIRE(old.restore(R"({"plotcore":1,"colours":{"mode":"same","single":"#336699"}})"));
        REQUIRE(old.settings().colours.custom);
    }
}

TEST_CASE("a window finer than what is held is drawn at what is held", "[plot]")
{
    // With nothing to read below the base, the closer look is the base itself
    // -- not the whole-line summary, which drew two points across this window.
    const std::vector<double> values = walk(1000000, 997);
    Plot plot;
    plot.setBudgetBytes(1 << 20);
    const int line = plot.addLine(values.data(), static_cast<long long>(values.size()));
    const long long base = plot.baseBucket(line);
    REQUIRE(base > 1);
    plot.setPaneColumns(704);
    plot.setView(4000.0, 4100.0, false);
    const PlotLine drawn = plot.drawLine(line);
    REQUIRE(drawn.summarised);
    REQUIRE(drawn.positions != nullptr);
    // Around the window the points are a base bucket's extremes apart, where
    // the summary's are hundreds of elements apart.
    std::vector<double> near;
    for (long long i = 0; i < drawn.count; ++i) {
        if (drawn.positions[i] >= 3000.0 && drawn.positions[i] < 5000.0) {
            near.push_back(drawn.positions[i]);
        }
    }
    REQUIRE(static_cast<long long>(near.size()) >= 2000 / base);
    for (std::size_t i = 1; i < near.size(); ++i) {
        REQUIRE(near[i] - near[i - 1] < 2.0 * static_cast<double>(base));
    }
    requireElements(drawn, values);
}

TEST_CASE("the crosshair reads nothing that is drawn off the pane", "[figure]")
{
    Figure figure = figureOf({ramp(10, 0.0, 1.0)});
    (void)figure.scene();
    figure.setViewRange(3.2, 3.4, 0.0, 10.0);
    const Scene scene = figure.scene();
    REQUIRE(scene.x.low == Approx(3.2));
    const PaneRect& a = scene.area;
    figure.hover(a.x + a.width / 2.0, a.y + a.height / 2.0);
    REQUIRE_FALSE(figure.scene().reading.valid);
}

TEST_CASE("a drawn line carries its points in the data's own units too", "[figure]")
{
    const std::vector<double> values = ramp(200, 10.0, 0.5);
    Figure figure = figureOf({values, ramp(200, 0.0, 1.0)});
    for (const bool flipped : {false, true}) {
        for (const bool yLog : {false, true}) {
            FigureSettings settings = figure.settings();
            settings.flipped = flipped;
            settings.yLog = yLog;
            figure.setSettings(settings);
            LineSettings own;
            own.ownAxis = true;
            figure.setLineSettings(0, own);
            const Scene scene = figure.scene();
            const DrawnLine& line = drawnOf(scene, 0);
            REQUIRE(line.own);
            REQUIRE(line.numbered);
            REQUIRE_FALSE(line.yLogarithmic); // an axis of its own is linear
            REQUIRE(line.values.size() == line.runs.size());
            const auto& run = line.values.front();
            REQUIRE(run.size() == 200);
            for (std::size_t i = 0; i < run.size(); ++i) {
                REQUIRE(run[i].x() == Approx(static_cast<double>(i)));
                REQUIRE(run[i].y() == Approx(values[i]));
            }
            const DrawnLine& other = drawnOf(scene, 1);
            REQUIRE_FALSE(other.own);
            REQUIRE(other.yLogarithmic == yLog);
            REQUIRE(other.y.low == Approx(scene.y.low));
            REQUIRE(other.y.high == Approx(scene.y.high));
            for (std::size_t i = 1; i < other.values.back().size(); ++i) {
                REQUIRE(other.values.back()[i].y() > other.values.back()[i - 1].y());
            }
        }
    }
}

// --- a host's own lines ---------------------------------------------------------

namespace {

/// A host's lines, as the viewer's models are to a figure: answered through
/// LineSource out of a Plot the host holds, every question counted. `copies`
/// hands the host's first line over that many times, which is how ten
/// thousand lines are drawn here without holding ten thousand.
class CountingSource : public LineSource
{
public:
    explicit CountingSource(Plot& plot, int copies = 0) : plot_(plot), copies_(copies) {}

    int lineCount() const override { return copies_ > 0 ? copies_ : plot_.lineCount(); }
    int series(int line) const override { return line + seriesOffset; }
    std::string name(int line) const override { return plot_.name(of(line)); }
    long long length(int line) const override { return plot_.length(of(line)); }
    bool ready(int line) const override { return plot_.ready(of(line)); }
    LineExtent extent(int line) const override
    {
        ++extents;
        return plot_.extent(of(line));
    }
    Span xExtent(int line) const override
    {
        ++xExtents;
        return plot_.xExtent(of(line));
    }
    double xPositiveMinimum(int line) const override { return plot_.xPositiveMinimum(of(line)); }
    void setPaneColumns(int columns) override
    {
        ++news;
        plot_.setPaneColumns(columns);
    }
    void setView(double xLow, double xHigh, bool xLog) override
    {
        ++news;
        plot_.setView(xLow, xHigh, xLog);
    }
    void focus(const GestureFocus& focus) override { focused.push_back(focus); }
    void clearFocus() override { ++cleared; }
    PlotLine drawLine(int line) const override { return plot_.drawLine(of(line)); }
    PlotAxis drawAxis(int line) const override { return plot_.drawAxis(of(line)); }

    int seriesOffset = 0;
    mutable int extents = 0;
    mutable int xExtents = 0;
    int news = 0;
    std::vector<GestureFocus> focused;
    int cleared = 0;

private:
    [[nodiscard]] int of(int line) const { return copies_ > 0 ? 0 : line; }

    Plot& plot_;
    int copies_;
};

Figure figureOver(LineSource& source, double width = 800.0, double height = 500.0)
{
    Figure figure;
    figure.setSource(&source);
    figure.setSize(width, height);
    FigureSettings settings = figure.settings();
    settings.legend = false;
    figure.setSettings(settings);
    return figure;
}

std::size_t pointsOf(const DrawnLine& line)
{
    std::size_t points = 0;
    for (const std::vector<Point>& run : line.runs) {
        points += run.size();
    }
    return points;
}

} // namespace

TEST_CASE("a figure draws a host's lines as it draws its own", "[figure][source]")
{
    const std::vector<double> first = walk(40000);
    const std::vector<double> second = ramp(3000, -5.0, 0.01);
    Figure own = figureOf({first, second});
    Plot held;
    held.addLine(first.data(), static_cast<long long>(first.size()), "line 0");
    held.addLine(second.data(), static_cast<long long>(second.size()), "line 1");
    CountingSource source(held);
    Figure hosted = figureOver(source);

    own.wheel(300.0, 200.0, 480.0, {});
    hosted.wheel(300.0, 200.0, 480.0, {});
    const Scene a = own.scene();
    const Scene b = hosted.scene();
    REQUIRE(a.area.x == b.area.x);
    REQUIRE(a.x == b.x);
    REQUIRE(a.y == b.y);
    REQUIRE(a.lines.size() == b.lines.size());
    for (std::size_t i = 0; i < a.lines.size(); ++i) {
        REQUIRE(a.lines[i].runs == b.lines[i].runs);
        REQUIRE(a.lines[i].colour == b.lines[i].colour);
    }
    REQUIRE(a.texts.size() == b.texts.size());

    // A line is coloured by the series it is, not by where it is drawn: the
    // host that left a line out before it hands over its series too.
    source.seriesOffset = 1;
    const Scene shifted = hosted.scene();
    REQUIRE(shifted.lines[0].colour == a.lines[1].colour);
    hosted.setSource(nullptr);
    REQUIRE(hosted.scene().lines.empty());
}

TEST_CASE("a zoom tells the lines where it is heading, and a pan takes it back", "[figure][source]")
{
    Plot held;
    const std::vector<double> values = walk(200000);
    held.addLine(values.data(), static_cast<long long>(values.size()), "walk");
    CountingSource source(held);
    Figure figure = figureOver(source);
    const PaneRect a = figure.scene().area;
    const double px = a.x + 0.25 * a.width;
    const double py = a.y + 0.5 * a.height;
    const double under = figure.camera().dataXAt(px, py);

    figure.wheel(px, py, 240.0, {});
    REQUIRE(source.focused.size() == 1);
    REQUIRE(source.focused.back().x == Approx(under));
    REQUIRE(source.focused.back().factor > 1.0);

    // y alone moves no x and is heading nowhere along it.
    figure.wheel(px, py, 240.0, Modifiers{false, true, false});
    REQUIRE(source.focused.size() == 1);

    const int cleared = source.cleared;
    figure.press(px, py, Button::Left, {});
    figure.move(px + 30.0, py, {});
    figure.release(px + 30.0, py, Button::Left, {});
    REQUIRE(source.cleared > cleared);

    figure.press(a.x + 0.4 * a.width, a.y + 0.2 * a.height, Button::Right, {});
    figure.move(a.x + 0.6 * a.width, a.y + 0.8 * a.height, {});
    figure.release(a.x + 0.6 * a.width, a.y + 0.8 * a.height, Button::Right, {});
    REQUIRE(source.focused.size() == 2);

    const int beforeReset = source.cleared;
    figure.doubleClick(px, py, Button::Left);
    REQUIRE(source.cleared == beforeReset + 1);
}

TEST_CASE("a frame asks each line what it spans once, however many lines there are",
          "[figure][source]")
{
    Plot held;
    const std::vector<double> values = walk(5000);
    held.addLine(values.data(), static_cast<long long>(values.size()), "walk");
    CountingSource source(held, 3000);
    Figure figure = figureOver(source);
    source.extents = 0;
    source.xExtents = 0;
    const Scene scene = figure.scene();
    REQUIRE(scene.lines.size() == 3000);
    REQUIRE(source.extents == 3000);
    REQUIRE(source.xExtents == 3000);
}

TEST_CASE("ten thousand lines share one budget of vertices", "[figure][source]")
{
    Plot held;
    const std::vector<double> values = walk(100000);
    held.addLine(values.data(), static_cast<long long>(values.size()), "walk");

    CountingSource one(held);
    Figure alone = figureOver(one, 1600.0, 500.0);
    const std::size_t full = pointsOf(alone.scene().lines.front());

    CountingSource many(held, 10000);
    Figure bundle = figureOver(many, 1600.0, 500.0);
    SceneRequest runsOnly;
    runsOnly.geometry.triangles = false;
    runsOnly.geometry.values = false;
    const Scene scene = bundle.scene(runsOnly);
    std::size_t total = 0;
    for (const DrawnLine& line : scene.lines) {
        total += pointsOf(line);
    }
    // Two stroke vertices a point: the whole frame within the budget, and
    // each line thinner than it is alone -- an envelope still, not a sample.
    REQUIRE(2 * total <= static_cast<std::size_t>(kMaxVertices) + 8 * scene.lines.size());
    REQUIRE(pointsOf(scene.lines.front()) < full);
    REQUIRE(pointsOf(scene.lines.front()) > static_cast<std::size_t>(maxColumnsFor(10000)));
}

TEST_CASE("a scene carries the forms of a line a host asks for and no others", "[figure]")
{
    Figure figure = figureOf({ramp(500, 0.0, 1.0), walk(20000)});
    SceneRequest strips;
    strips.geometry.runs = false;
    strips.geometry.triangles = false;
    strips.geometry.values = false;
    strips.geometry.strips = true;
    const Scene lean = figure.scene(strips);
    const Scene full = figure.scene();
    REQUIRE(lean.lines.size() == full.lines.size());
    for (std::size_t i = 0; i < lean.lines.size(); ++i) {
        REQUIRE(lean.lines[i].runs.empty());
        REQUIRE(lean.lines[i].triangles.empty());
        REQUIRE(lean.lines[i].values.empty());
        REQUIRE(lean.lines[i].strips.size() == full.lines[i].runs.size());
        std::size_t triangles = 0;
        for (std::size_t r = 0; r < lean.lines[i].strips.size(); ++r) {
            const std::size_t points = full.lines[i].runs[r].size();
            if (points > 1) {
                REQUIRE(lean.lines[i].strips[r].size() == 2 * points);
                triangles += 3 * (2 * points - 2);
            }
        }
        // The same triangles, a third of the points.
        REQUIRE(full.lines[i].triangles.size() == triangles);
    }
}

TEST_CASE("a picture is the pane laid out again, and asks the lines for nothing",
          "[figure][source]")
{
    Plot held;
    const std::vector<double> values = ramp(400, 10.0, 0.5);
    held.addLine(values.data(), static_cast<long long>(values.size()), "ramp");
    CountingSource source(held);
    Figure figure = figureOver(source);
    const Scene pane = figure.scene();
    const Point at = sampleAt(pane, 0, 150);
    figure.hover(at.x(), at.y());
    figure.press(pane.area.x + 10.0, pane.area.y + 10.0, Button::Right, {});
    figure.move(pane.area.x + 200.0, pane.area.y + 150.0, {});
    const Scene live = figure.scene();
    REQUIRE(live.band.has_value());

    SceneRequest request;
    request.picture = true;
    request.width = 1600.0;
    request.height = 1000.0;
    request.paper = true;
    request.transparent = true;
    const int news = source.news;
    const Scene picture = figure.scene(request);
    REQUIRE(source.news == news);
    REQUIRE(picture.width == 1600.0);
    REQUIRE(picture.height == 1000.0);
    REQUIRE(picture.area.width > 2.0 * pane.area.width - 200.0);
    REQUIRE_FALSE(picture.band.has_value());
    REQUIRE_FALSE(picture.reading.valid);
    REQUIRE(picture.ground.alpha == 0.0);
    REQUIRE(picture.chrome.ink == chromeFor(true, true).ink);
    REQUIRE(picture.x == live.x);

    // ...and the pane is the pane again afterwards, gestures and all.
    const Scene again = figure.scene();
    REQUIRE(again.area.x == live.area.x);
    REQUIRE(again.area.width == live.area.width);
    REQUIRE(again.band.has_value());
    REQUIRE(again.chrome.ink == live.chrome.ink);
    figure.release(pane.area.x + 200.0, pane.area.y + 150.0, Button::Right, {});

    // A crosshair named rather than pointed at lands where the picture drew
    // its sample, which is the same share of the pane the pane drew it at.
    figure.hover(at.x(), at.y());
    const Scene hovered = figure.scene();
    REQUIRE(hovered.reading.valid);
    request.reading = PinnedSample{hovered.reading.line, hovered.reading.x, hovered.reading.y};
    const Scene pinned = figure.scene(request);
    REQUIRE(pinned.reading.valid);
    REQUIRE(pinned.reading.x == hovered.reading.x);
    REQUIRE((pinned.reading.at.x() - pinned.area.x) / pinned.area.width ==
            Approx((hovered.reading.at.x() - hovered.area.x) / hovered.area.width));
    REQUIRE((pinned.reading.at.y() - pinned.area.y) / pinned.area.height ==
            Approx((hovered.reading.at.y() - hovered.area.y) / hovered.area.height));
}
