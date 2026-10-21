// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: BSD-3-Clause

// The ladder of runs, held for a group of lines and read through a host.
//
// RunLadder's reads are the host's, so a test is the host: it lends the
// pyramids, performs each read it is told to by taking the elements out of the
// line it built them from, and counts what is given up. Nothing here waits on
// a clock, and everything is a count of runs, reads and doubles.

#include "plotcore/RunLadder.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

using namespace plotcore;

namespace {

constexpr long long kLength = 100000;

std::vector<double> lineOf(double phase)
{
    std::vector<double> values(static_cast<std::size_t>(kLength));
    for (long long i = 0; i < kLength; ++i) {
        values[static_cast<std::size_t>(i)] = std::sin(static_cast<double>(i) / 700.0 + phase);
    }
    return values;
}

LevelView viewOver(double low, double high)
{
    LevelView view;
    view.length = kLength;
    view.low = low;
    view.high = high;
    view.paneBuckets = 500;
    view.detailBuckets = 1000;
    view.prefetchOctaves = 2;
    return view;
}

/// A host: the lines, the pyramids it holds of them at `base`, and a reader
/// that answers a run out of the elements, the way a file would.
struct Host
{
    explicit Host(int lines, long long base)
    {
        for (int line = 0; line < lines; ++line) {
            values.push_back(lineOf(line));
            pyramids.push_back(plotcore::pyramidOf(values.back().data(), kLength, base));
        }
    }

    [[nodiscard]] RunLadder::PyramidOf lent() const
    {
        return [this](int line) -> const LinePyramid* {
            return line >= 0 && static_cast<std::size_t>(line) < pyramids.size()
                       ? &pyramids[static_cast<std::size_t>(line)]
                       : nullptr;
        };
    }

    /// Read `read` and land it, as a reply would: each line's elements over
    /// the window, at a bucket of one.
    void answer(RunLadder& ladder, const RunRead& read)
    {
        ++reads;
        for (const int line : read.lines) {
            const auto& all = values[static_cast<std::size_t>(line)];
            std::vector<double> run(all.begin() + read.window.first,
                                    all.begin() + read.window.first + read.window.span);
            ladder.land(read.window, line, 1.0, std::move(run));
        }
    }

    std::vector<std::vector<double>> values;
    std::vector<LinePyramid> pyramids;
    int reads = 0;
};

} // namespace

TEST_CASE("a run its pyramids can answer is folded at once, and nothing is read", "[runs]")
{
    Host host(1, 1);
    RunLadder ladder;
    ladder.setLines({0});
    ladder.setView(viewOver(40000.0, 41000.0));

    REQUIRE(ladder.refresh(host.lent()));
    const RunLadder::Run* drawn = ladder.drawn();
    REQUIRE(drawn != nullptr);
    CHECK(drawn->window.covers(40000.0, 41000.0));
    // What a fold of the pyramid at that window is, value for value.
    std::vector<double> folded;
    REQUIRE(fillWindow(host.pyramids[0], drawn->window, folded));
    CHECK(drawn->lines.at(0).values == folded);
    // Every rung the policy wants is folded in the same call, so there is
    // nothing left to read.
    CHECK_FALSE(ladder.read(host.lent()).has_value());
    CHECK(ladder.runs().size() > 1);
}

TEST_CASE("a run finer than the pyramid's base is a read, named once and drawn when it lands",
          "[runs]")
{
    // A base of sixty-four: what a line too long to hold at one is held at.
    Host host(1, 64);
    RunLadder ladder;
    ladder.setLines({0});
    ladder.setView(viewOver(40000.0, 40100.0));
    ladder.refresh(host.lent());

    const std::optional<RunRead> read = ladder.read(host.lent());
    REQUIRE(read.has_value());
    CHECK(read->window.bucket < 64);
    CHECK(read->lines == std::vector<int>{0});
    CHECK(read->window.covers(40000.0, 40100.0));
    CHECK_FALSE(ladder.folds(read->window, host.lent()));

    // Still named while it is out: whether to ask again is the host's, which
    // reads one at a time or batches it with other groups'.
    CHECK(ladder.read(host.lent())->window == read->window);
    host.answer(ladder, *read);
    const std::optional<RunRead> after = ladder.read(host.lent());
    CHECK((!after.has_value() || after->window != read->window));
    const RunLadder::Run* drawn = ladder.drawn();
    REQUIRE(drawn != nullptr);
    CHECK(drawn->window == read->window);
    CHECK(drawn->lines.at(0).values.front() == host.values[0][read->window.first]);
}

TEST_CASE("a group's run is drawn only once every line of it has come back", "[runs]")
{
    Host host(2, 64);
    RunLadder ladder;
    ladder.setLines({0, 1});
    ladder.setView(viewOver(40000.0, 40100.0));
    ladder.refresh(host.lent());

    const std::optional<RunRead> read = ladder.read(host.lent());
    REQUIRE(read.has_value());
    // One read for the group: a table's rows are one hyperslab of the file.
    CHECK(read->lines == std::vector<int>{0, 1});

    const auto& all = host.values[0];
    ladder.land(read->window, 0, 1.0,
                std::vector<double>(all.begin() + read->window.first,
                                    all.begin() + read->window.first + read->window.span));
    const RunLadder::Run* half = ladder.drawn();
    CHECK((half == nullptr || half->window != read->window));
    // ...and what is left to read is the other line alone.
    const std::optional<RunRead> rest = ladder.read(host.lent());
    REQUIRE(rest.has_value());
    CHECK(rest->lines == std::vector<int>{1});

    host.answer(ladder, *rest);
    REQUIRE(ladder.drawn() != nullptr);
    CHECK(ladder.drawn()->window == read->window);
}

TEST_CASE("a group's run is drawn when one line of it came back with nothing, and that line is "
          "not drawn from it",
          "[runs]")
{
    Host host(2, 64);
    RunLadder ladder;
    ladder.setLines({0, 1});
    ladder.setView(viewOver(40000.0, 40100.0));
    ladder.refresh(host.lent());
    const std::optional<RunRead> read = ladder.read(host.lent());
    REQUIRE(read.has_value());

    const auto& all = host.values[0];
    ladder.land(read->window, 0, 1.0,
                std::vector<double>(all.begin() + read->window.first,
                                    all.begin() + read->window.first + read->window.span));
    ladder.land(read->window, 1, 1.0, {});
    // Drawn: the line that read is the closer look, and the one that did not
    // is the host's to draw from its whole-line summary, as a table's line
    // that will not read closely always was.
    const RunLadder::Run* drawn = ladder.drawn();
    REQUIRE(drawn != nullptr);
    CHECK(drawn->window == read->window);
    CHECK(drawn->of(0) != nullptr);
    CHECK(drawn->of(1) == nullptr);
    // ...and neither is asked for again.
    const std::optional<RunRead> again = ladder.read(host.lent());
    CHECK((!again.has_value() || again->window != read->window));
}

TEST_CASE("a line that leaves the group is given up from every run, and one that joins is read "
          "alone",
          "[runs]")
{
    Host host(3, 64);
    long long retired = 0;
    RunLadder ladder([&](std::vector<double>& values) {
        retired += static_cast<long long>(values.size());
        values.clear();
    });
    ladder.setLines({0, 1});
    ladder.setView(viewOver(40000.0, 40100.0));
    ladder.refresh(host.lent());
    const std::optional<RunRead> read = ladder.read(host.lent());
    REQUIRE(read.has_value());
    host.answer(ladder, *read);
    REQUIRE(ladder.drawn() != nullptr);
    const long long before = ladder.heldDoubles();

    // Line 1 unticked: what it held goes, through the host, and the run is
    // still drawn for line 0.
    ladder.setLines({0});
    CHECK(retired > 0);
    CHECK(ladder.heldDoubles() == before - retired);
    for (const RunLadder::Run& run : ladder.runs()) {
        CHECK(run.lines.find(1) == run.lines.end());
    }
    REQUIRE(ladder.drawn() != nullptr);
    CHECK(ladder.drawn()->window == read->window);

    // Line 2 ticked: the run in hand cannot draw it, so it is not drawn until
    // line 2 alone has been read at it.
    ladder.setLines({0, 2});
    const RunLadder::Run* drawn = ladder.drawn();
    CHECK((drawn == nullptr || drawn->window != read->window));
    const std::optional<RunRead> rest = ladder.read(host.lent());
    REQUIRE(rest.has_value());
    CHECK(rest->window == read->window);
    CHECK(rest->lines == std::vector<int>{2});
    host.answer(ladder, *rest);
    REQUIRE(ladder.drawn() != nullptr);
    CHECK(ladder.drawn()->window == read->window);
}

TEST_CASE("a line that came back with nothing is not asked for again, nor drawn from", "[runs]")
{
    Host host(1, 64);
    RunLadder ladder;
    ladder.setLines({0});
    ladder.setView(viewOver(40000.0, 40100.0));
    ladder.refresh(host.lent());
    const std::optional<RunRead> read = ladder.read(host.lent());
    REQUIRE(read.has_value());

    ladder.land(read->window, 0, 1.0, {});
    CHECK(ladder.holds(read->window));
    const std::optional<RunRead> again = ladder.read(host.lent());
    CHECK((!again.has_value() || again->window != read->window));
    const RunLadder::Run* drawn = ladder.drawn();
    CHECK((drawn == nullptr || drawn->window != read->window));
}

TEST_CASE("runs past the count held are given up coldest first, through the host's retire",
          "[runs]")
{
    Host host(1, 1);
    long long retired = 0;
    int given = 0;
    RunLadder ladder([&](std::vector<double>& values) {
        retired += static_cast<long long>(values.size());
        ++given;
        values.clear();
    });
    ladder.setLines({0});
    ladder.setHeld(3);

    // A zoom of ten octaves about one point, a frame at a time, each
    // answered out of the pyramid.
    double low = 0.0;
    double high = static_cast<double>(kLength);
    const double focus = 31415.0;
    for (int frame = 0; frame < 10; ++frame) {
        low = focus - (focus - low) / 2.0;
        high = focus + (high - focus) / 2.0;
        PlotFocus heading;
        heading.position = focus;
        heading.active = true;
        ladder.setView(viewOver(low, high), heading);
        ladder.refresh(host.lent());
        CHECK(ladder.runs().size() <= 3);
        // Out at the first frames the whole-line summary is what the pane
        // needs; from there in, a run covering the view is drawn.
        if (!ladder.zoomedOut()) {
            REQUIRE(ladder.drawn() != nullptr);
            CHECK(ladder.drawn()->window.covers(std::max(low, 0.0), high));
        }
    }
    REQUIRE_FALSE(ladder.zoomedOut());
    CHECK(given > 0);
    CHECK(retired > 0);
    long long held = 0;
    for (const RunLadder::Run& run : ladder.runs()) {
        held += static_cast<long long>(run.lines.at(0).values.size() +
                                       run.lines.at(0).positions.size());
    }
    CHECK(ladder.heldDoubles() == held);

    SECTION("and every one goes once the view is out past them all")
    {
        ladder.setView(viewOver(0.0, static_cast<double>(kLength)));
        CHECK(ladder.zoomedOut());
        CHECK(ladder.drawn() == nullptr);
        const int before = given;
        ladder.clear();
        CHECK(ladder.runs().empty());
        CHECK(given > before);
        CHECK(ladder.heldDoubles() == 0);
    }

    SECTION("and a ladder moved over this one gives up what it held through its retire")
    {
        const int before = given;
        ladder = RunLadder();
        CHECK(given > before);
        CHECK(ladder.runs().empty());
    }
}
