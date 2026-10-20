// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// The runs a group of lines is held at besides its whole, and the one to read
// next.
//
// Both of the viewer's plots hold, for every drawn line, a ladder of closer
// looks: the run the pane is on, the octaves read ahead towards the pointer
// and out of it, and the runs the reader has already been through, kept so
// that zooming back the way they came costs nothing. A run is folded out of
// the line's pyramid when the pyramid can answer it, read from the file when
// it cannot, and given up coldest first when there are more than the budget
// holds. Which run is drawn, which is wanted and which goes was already
// decided here, in Levels; the ladder those three are asked about was still
// written twice in the viewer -- once for a table's rows, read together, and
// once for a custom tab's entries, read one by one. Two copies of one thing
// drift: when everything about a run was written twice, a custom tab read a
// pane's run one bucket at a time, two thousand reads per entry where the
// Plot tab made one, and no test counted. The policy became one definition
// then. This is the ladder it is asked about, once.
//
// It holds runs for a *group* of lines that share one view and are read
// together: every drawn row of a table, which are one hyperslab of the file;
// or one line alone, which is a custom tab's entry or a time base. A run is a
// window and a run of each line at it, and it is drawn once every line of the
// group has come back -- and at least one of them with something, because a
// line that will not read closely is drawn from its whole-line summary and a
// run of nothing but such lines is no closer look at all. HeldLevel::complete
// was the one question the two plots answered differently, a table by "every
// line" and a custom tab by "with something"; for a group of one they are the
// same question, so this asks both.
//
// Three things it deliberately does not do, because each is the host's:
//
// - **Hold the pyramids.** A pyramid is lent for the call that folds out of
//   it (PyramidOf). Where it lives, how it was read and what budget it was
//   fitted to are the host's.
// - **Read.** A read below the pyramids' bases is something this names
//   (read()) and a host performs -- on whatever thread it reads on, after
//   whatever settle it waits out, one at a time or batched with other groups'
//   -- and answers when it can (land()). A table reads its group in one
//   hyperslab; a custom tab reads every entry's run in one crossing of the
//   thread. Neither is a question of the ladder's. What a read folds is the
//   run *and the element after it*, where the line has one: that element is
//   the seam of the run's last bucket (see Levels.hpp), and a run folded out of
//   a pyramid has it, so a read that left it out would draw its last column
//   short of the same run folded. A read that arrives in pieces joins each
//   piece's last seam when the next piece lands (joinSeam).
// - **Free what a renderer may be drawing.** A run given up is handed, vector
//   by vector, to `Retire`, which is the host's borrow contract: the viewer
//   keeps them until the next fill hands its renderer something else.

#include "plotcore/Levels.hpp"
#include "plotcore/Projection.hpp"
#include "plotcore/Pyramid.hpp"

#include <functional>
#include <map>
#include <optional>
#include <span>
#include <type_traits>
#include <vector>

namespace plotcore {

/// One read below the pyramids' bases: a window, and the lines of the group
/// not yet held at it.
struct RunRead
{
    PlotWindow window;
    std::vector<int> lines;
};

class RunLadder
{
public:
    /// One line's run: its values at the window's bucket and, for an
    /// envelope, the element each one is (see PlotLine::positions); empty
    /// where the values are the elements.
    struct LineRun
    {
        std::vector<double> values;
        std::vector<double> positions;
    };

    /// One window, and each line of the group at it.
    struct Run
    {
        Run(const PlotWindow& at, double by) : window(at), step(by) {}

        // Relocated by moving, never by copying -- and the copy is *deleted*
        // rather than merely never written, because deleting it is the only
        // thing that makes that true.
        //
        // The runs are a std::vector of these, and a std::vector reallocates
        // with std::move_if_noexcept: it takes the copy constructor whenever
        // the element's move constructor is not `noexcept` and a copy exists,
        // so that a throw half way through leaves the old storage intact. A
        // copy here is the use-after-free. It deep-copies every mapped vector
        // into the new storage and then destroys the originals -- freeing the
        // exact buffers a renderer is holding a pointer into, while it is
        // drawing them.
        //
        // Whether that happened was down to the standard library. std::map's
        // move constructor is noexcept on libstdc++ and libc++, and is not on
        // MSVC's, whose tree keeps a sentinel node that the move allocates. So
        // in the viewer, whose table plot held its runs this way before this
        // class did, Linux moved and passed, Windows copied, and the second
        // closer look to land -- the push_back that grew the runs from one to
        // two -- segfaulted in the test that dereferences the held line to
        // prove it is still there. With no copy to fall back on the relocation
        // is a move on every library, and a std::map move relinks nodes in
        // constant time: every mapped vector stays at the address the renderer
        // was given.
        Run(Run&&) = default;
        Run& operator=(Run&&) = default;
        Run(const Run&) = delete;
        Run& operator=(const Run&) = delete;
        ~Run() = default;

        PlotWindow window;
        /// Elements between one drawn point and the next: half a bucket, or
        /// one when the run is the elements themselves.
        double step = 1.0;
        std::map<int, LineRun> lines;

        /// Line `line`'s run, or null where it was not read at this window or
        /// came back with nothing: what a host draws from, falling back to the
        /// line's whole-line summary on null.
        [[nodiscard]] const LineRun* of(int line) const
        {
            const auto found = lines.find(line);
            return found != lines.end() && !found->second.values.empty() ? &found->second : nullptr;
        }
    };

    // Asserted rather than left to the one platform that would notice. A
    // member added later that made a Run copyable again would put the copy back
    // on MSVC's library and nowhere else, which is a segfault on Windows and a
    // green run everywhere a reader would be looking.
    static_assert(!std::is_copy_constructible_v<Run>,
                  "a Run must relocate by moving: a copy frees the values a renderer borrows");

    /// Where a vector given up goes. It may still be borrowed by a renderer,
    /// so it is the host's to keep until nothing is drawing it. The default
    /// frees it at once.
    using Retire = std::function<void(std::vector<double>&)>;
    /// The pyramid line `line` folds out of, or null where it has none; lent
    /// for the call it is passed to.
    using PyramidOf = std::function<const LinePyramid*(int line)>;

    RunLadder() = default;
    explicit RunLadder(Retire retire) : retire_(std::move(retire)) {}
    ~RunLadder();
    RunLadder(RunLadder&&) noexcept = default;
    /// Gives up what this held, through its own retire, before it takes
    /// another's: dropped silently, a run a renderer is drawing would be freed
    /// under it.
    RunLadder& operator=(RunLadder&& other) noexcept;
    RunLadder(const RunLadder&) = delete;
    RunLadder& operator=(const RunLadder&) = delete;

    void setRetire(Retire retire) { retire_ = std::move(retire); }
    /// The lines of the group: a run must hold every one to be drawn. A line
    /// that leaves the group is given up from every run, through retire; one
    /// that joins it is read at the runs already held (read()), and until then
    /// those runs are not drawn.
    void setLines(std::vector<int> lines);
    [[nodiscard]] const std::vector<int>& lines() const { return lines_; }
    /// What is being looked at, in the group's own element positions, and
    /// where a zoom is heading. An unusable view is how "no closer look at
    /// all" is said: nothing is drawn from a run, wanted or read.
    void setView(const LevelView& view, const PlotFocus& focus = {});
    [[nodiscard]] const LevelView& view() const { return view_; }
    [[nodiscard]] const PlotFocus& focus() const { return focus_; }
    /// Runs held at most: kHeldLevels, or fewer where the budget says so.
    void setHeld(int count) { held_ = count; }
    [[nodiscard]] int heldCount() const { return held_; }

    // --- the policy --------------------------------------------------------------
    /// The run over the view at `buckets`, or nothing when the view is
    /// unusable or a run that coarse is the whole line.
    [[nodiscard]] std::optional<PlotWindow> window(int buckets) const;
    /// Whether the view is out past every run worth holding: what the pane
    /// needs is the whole-line summary, and every run can go (clear()).
    [[nodiscard]] bool zoomedOut() const { return !window(view_.paneBuckets).has_value(); }
    /// The runs as Levels sees them.
    [[nodiscard]] std::span<const HeldLevel> held() const;
    /// The run to draw, or null for the whole-line summary.
    [[nodiscard]] const Run* drawn() const;
    /// The run worth having next, held or not.
    [[nodiscard]] std::optional<PlotWindow> wanted() const;
    /// Whether every line's pyramid can answer `window` without a read.
    [[nodiscard]] bool folds(const PlotWindow& window, const PyramidOf& pyramidOf) const;
    /// Whether a run is held at `window`.
    [[nodiscard]] bool holds(const PlotWindow& window) const;

    // --- holding -----------------------------------------------------------------
    /// Fold everything the pyramids can answer, now: the run the pane is on
    /// at the detail bucket, then the ladder the policy wants. A fold waits on
    /// nothing, so there is no reason to answer one and wait for the next.
    /// True when anything was folded.
    bool refresh(const PyramidOf& pyramidOf);
    /// Fold `window` alone for every line that lacks it: what a time base
    /// holds, which is the run the pane is on and no octave read ahead -- a
    /// fold costs nothing, so a run of an axis held against a zoom nobody has
    /// made yet is memory spent on arithmetic that is already free. False when
    /// the pyramids cannot answer it, or every line has it already.
    bool fold(const PlotWindow& window, const PyramidOf& pyramidOf);
    /// The read worth asking for, if any: the run the policy wants next, when
    /// the pyramids cannot answer it and some line of the group lacks it.
    [[nodiscard]] std::optional<RunRead> read(const PyramidOf& pyramidOf) const;
    /// What a read brought back for `line` at `window`, its points `step`
    /// elements apart. Empty values are kept too, as "this will not read", so
    /// the run is not asked for again -- and are not drawn from.
    void land(const PlotWindow& window, int line, double step, std::vector<double> values,
              std::vector<double> positions = {});
    /// Give up runs past the count held, coldest first.
    void trim();
    /// Give up every run.
    void clear();

    [[nodiscard]] const std::vector<Run>& runs() const { return runs_; }
    /// Doubles held in runs.
    [[nodiscard]] long long heldDoubles() const;

private:
    [[nodiscard]] bool complete(const Run& run) const;
    [[nodiscard]] Run* runAt(const PlotWindow& window);
    void retire(Run& run);
    void retire(std::vector<double>& values);

    Retire retire_;
    std::vector<int> lines_;
    LevelView view_;
    PlotFocus focus_;
    int held_ = kHeldLevels;
    std::vector<Run> runs_;
    /// What held() hands Levels, kept so that asking costs no allocation.
    mutable std::vector<HeldLevel> ladder_;
};

} // namespace plotcore
