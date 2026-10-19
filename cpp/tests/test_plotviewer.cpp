// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: GPL-3.0-only

// The viewer's answers, asked again.
//
// Format, Ticks, Camera and Palettes began as ports of the viewer's QML --
// PlotFrame.qml's numbering and ruling, PlotSurface.qml's zoom and pan,
// Theme.qml's colours -- and the viewer held the two to each other with a
// parity suite that asked both the same questions and compared the answers
// exactly. That suite could only run where the QML was, and the QML goes: the
// viewer now draws through Figure, so the ports are the only implementation
// left and there is nothing on the other side to ask.
//
// So the questions were written down while there still was. viewer/answers.jsonl
// is every question tst_plotcore_parity.qml asked, with the answer it was
// given, recorded while that suite passed -- so every answer in it is the
// viewer's own answer as well as this library's (viewer/README.md says how).
// This asks each one again and holds the answer to it: a label character for
// character, a number to the golden digest's tolerance (see same()).

#include "Json.hpp"
#include "plotcore/Camera.hpp"
#include "plotcore/Format.hpp"
#include "plotcore/Palettes.hpp"
#include "plotcore/Ticks.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace plotcore;
using json::Value;

namespace {

// --- the record's encoding ----------------------------------------------------
// A double is written as `%.17g` in a string, which keeps a NaN, an infinity
// and a negative zero that JSON's own numbers cannot.

double numberOf(const Value& value)
{
    if (value.kind == Value::Kind::Number) {
        return value.number;
    }
    return std::strtod(value.text.c_str(), nullptr);
}

int integerOf(const Value& value)
{
    return static_cast<int>(numberOf(value));
}

std::vector<double> numbersOf(const Value& value)
{
    std::vector<double> out;
    for (const Value& item : value.items) {
        out.push_back(numberOf(item));
    }
    return out;
}

Rgba rgbaOf(const Value& value)
{
    return Rgba::of(numberOf(value.items[0]), numberOf(value.items[1]), numberOf(value.items[2]),
                    numberOf(value.items[3]));
}

TickSet tickSetOf(const Value& value)
{
    TickSet set;
    set.values = numbersOf(*value.find("values"));
    set.ticks = numbersOf(*value.find("ticks"));
    for (const Value& label : value.find("texts")->items) {
        set.texts.push_back(
            TickLabel{label.items[0].text, label.items[1].text, label.items[2].text});
    }
    set.logarithmic = value.boolOr("logarithmic", false);
    set.fallback = value.boolOr("fallback", false);
    return set;
}

GridMode modeOf(const std::string& mode)
{
    if (mode == "none") {
        return GridMode::None;
    }
    if (mode == "dense") {
        return GridMode::Dense;
    }
    if (mode == "custom") {
        return GridMode::Custom;
    }
    return GridMode::Loose;
}

// --- an answer, in the same shape ---------------------------------------------

Value number(double value)
{
    return Value::of(value);
}

Value numbers(const std::vector<double>& values)
{
    Value out = Value::array();
    for (const double v : values) {
        out.items.push_back(number(v));
    }
    return out;
}

Value rgba(const Rgba& colour)
{
    return numbers({colour.red, colour.green, colour.blue, colour.alpha});
}

Value text(const std::string& value)
{
    return Value::of(value);
}

Value tickSet(const TickSet& set)
{
    Value texts = Value::array();
    for (const TickLabel& label : set.texts) {
        Value one = Value::array();
        one.items = {text(label.base), text(label.exponent), text(label.text)};
        texts.items.push_back(std::move(one));
    }
    Value out = Value::object();
    out.set("values", numbers(set.values))
        .set("ticks", numbers(set.ticks))
        .set("texts", std::move(texts))
        .set("logarithmic", Value::of(set.logarithmic))
        .set("fallback", Value::of(set.fallback));
    return out;
}

Value pair(double first, double second)
{
    return numbers({first, second});
}

/// Whether the recorded answer and this one are the same answer: a string
/// character for character, everything else member for member, and a number
/// within the golden digest's relative 1e-9 -- two NaNs being one, and a
/// zero's sign counting. Not bit for bit, as the viewer's suite compared:
/// these were recorded with glibc's libm, and another platform's log is
/// allowed to round its last bit the other way, which on MSVC moves a few of
/// the fractions an ulp or five. A label rounded from such a number would
/// show it, and a label is held exactly.
bool same(const Value& recorded, const Value& answer, std::string& why)
{
    if (answer.kind == Value::Kind::Number) {
        const double was = numberOf(recorded);
        const double is = answer.number;
        const bool equal =
            (std::isnan(was) && std::isnan(is)) ||
            (was == is && std::signbit(was) == std::signbit(is)) ||
            (std::isfinite(was) && std::isfinite(is) &&
             std::abs(was - is) <= 1e-9 * std::max({std::abs(was), std::abs(is), 1e-300}));
        if (!equal) {
            why = recorded.text + " against " + toPrecision(is, 17);
        }
        return equal;
    }
    if (answer.kind == Value::Kind::String) {
        if (recorded.text != answer.text) {
            why = "\"" + recorded.text + "\" against \"" + answer.text + "\"";
            return false;
        }
        return true;
    }
    if (answer.kind == Value::Kind::Bool) {
        if (recorded.boolean != answer.boolean) {
            why = "a boolean";
            return false;
        }
        return true;
    }
    if (answer.kind == Value::Kind::Array) {
        if (recorded.items.size() != answer.items.size()) {
            why = std::to_string(recorded.items.size()) + " items against " +
                  std::to_string(answer.items.size());
            return false;
        }
        for (std::size_t i = 0; i < answer.items.size(); ++i) {
            if (!same(recorded.items[i], answer.items[i], why)) {
                why = "[" + std::to_string(i) + "] " + why;
                return false;
            }
        }
        return true;
    }
    if (answer.kind == Value::Kind::Object) {
        for (const auto& [key, member] : answer.members) {
            const Value* found = recorded.find(key);
            if (found == nullptr || !same(*found, member, why)) {
                why = key + ": " + (found == nullptr ? std::string("missing") : why);
                return false;
            }
        }
        return true;
    }
    return true;
}

LineColours coloursOf(const Value& value)
{
    LineColours colours;
    colours.mode = value.textOr("mode", "");
    colours.single = rgbaOf(*value.find("single"));
    colours.rangeFrom = rgbaOf(*value.find("rangeFrom"));
    colours.rangeTo = rgbaOf(*value.find("rangeTo"));
    colours.reversed = value.boolOr("reversed", false);
    colours.from = numberOf(*value.find("from"));
    colours.to = numberOf(*value.find("to"));
    colours.custom = value.boolOr("custom", false);
    return colours;
}

/// This library's answer to one recorded question.
Value answer(const std::string& f, const std::vector<Value>& a,
             const std::vector<LineColours>& colours)
{
    const auto n = [&](std::size_t i) { return numberOf(a[i]); };
    const auto b = [&](std::size_t i) { return a[i].boolean; };
    const auto k = [&](std::size_t i) { return integerOf(a[i]); };
    if (f == "toFixed") {
        return text(toFixed(n(0), k(1)));
    }
    if (f == "toExponential") {
        return text(toExponential(n(0), k(1)));
    }
    if (f == "toPrecision") {
        return text(toPrecision(n(0), k(1)));
    }
    if (f == "numberText") {
        return text(numberText(n(0)));
    }
    if (f == "jsRound") {
        return number(jsRound(n(0)));
    }
    if (f == "niceStep") {
        return number(niceStep(n(0), n(1)));
    }
    if (f == "decimalsFor") {
        return number(decimalsFor(n(0)));
    }
    if (f == "labelFor") {
        return text(labelFor(n(0), n(1), n(2)));
    }
    if (f == "logLabelFor") {
        return text(logLabelFor(n(0)));
    }
    if (f == "fractionOn") {
        return number(fractionOn(n(0), n(1), n(2), b(3), n(4)));
    }
    if (f == "logLocate") {
        const LogLocated located = logLocate(n(0), n(1), n(2), k(3), k(4));
        Value out = Value::object();
        out.set("majors", numbers(located.majors))
            .set("minors", numbers(located.minors))
            .set("stride", number(located.stride))
            .set("linear", Value::of(located.linear));
        return out;
    }
    if (f == "logSublabels") {
        return numbers(logSublabels(n(0), n(1), n(2)));
    }
    if (f == "tickValues") {
        TickStyle style;
        style.target = k(6);
        style.fontPixels = n(7);
        return tickSet(tickValues(n(0), n(1), b(2), n(3), n(4), b(5), style));
    }
    if (f == "gridValues") {
        TickStyle style;
        style.target = k(5);
        style.denseFactor = k(6);
        return numbers(gridValues(modeOf(a[0].text), n(1), n(2), n(3), tickSetOf(a[4]), style));
    }
    if (f == "minorNumbersOf") {
        Value out = Value::array();
        for (const MinorNumber& found :
             minorNumbersOf(b(0), modeOf(a[1].text), numbersOf(a[2]), tickSetOf(a[3]), n(4), n(5),
                            n(6), n(7), n(8), n(9))) {
            Value one = Value::array();
            one.items = {number(found.at), text(found.text)};
            out.items.push_back(std::move(one));
        }
        return out;
    }
    if (f == "padded") {
        const Span span = padded(n(0), n(1), b(2), n(3));
        return pair(span.low, span.high);
    }
    if (f == "valueAlong") {
        return number(valueAlong(n(0), n(1), n(2), b(3), n(4)));
    }
    if (f == "visibleLow") {
        return number(visibleLow(n(0), n(1), n(2), n(3), b(4), n(5)));
    }
    if (f == "visibleHigh") {
        return number(visibleHigh(n(0), n(1), n(2), n(3), b(4), n(5)));
    }
    if (f == "clampPan") {
        return number(clampPan(n(0), n(1), n(2), n(3), b(4), n(5), n(6)));
    }
    if (f == "logZoomCeiling") {
        return number(logZoomCeiling(n(0), n(1), n(2), n(3), n(4), n(5)));
    }
    if (f == "zoomedAxis") {
        const AxisZoom next =
            zoomedAxis({n(0), n(1)}, n(2), n(3), n(4), n(5), b(6), n(7), n(8), n(9), n(10));
        return pair(next.zoom, next.pan);
    }
    if (f == "viewedAxis") {
        const AxisZoom view = viewedAxis(n(0), n(1), n(2), n(3), b(4), n(5), n(6), n(7), n(8));
        return pair(view.zoom, view.pan);
    }
    if (f == "positiveSpan") {
        const Span span = positiveSpan(n(0), n(1), n(2), n(3), b(4));
        return pair(span.low, span.high);
    }
    if (f == "palette" || f == "ramp") {
        const auto stops = f == "palette" ? palette(a[0].text, b(1)) : ramp(a[0].text);
        Value out = Value::array();
        for (const Rgba& stop : stops.value_or(std::vector<Rgba>{})) {
            out.items.push_back(rgba(stop));
        }
        return out;
    }
    if (f == "rampColor") {
        return rgba(rampColor(ramp(a[0].text).value_or(std::vector<Rgba>{}), n(1)));
    }
    if (f == "paperColor") {
        // Its one argument, a colour, was recorded as the arguments themselves.
        Value colour = Value::array();
        colour.items = a;
        return rgba(paperColor(rgbaOf(colour)));
    }
    if (f == "seriesColor") {
        std::optional<Rgba> own;
        if (a[6].kind == Value::Kind::Array) {
            own = rgbaOf(a[6]);
        }
        return rgba(seriesColor(colours[static_cast<std::size_t>(k(0))], k(1), k(2), k(3), b(4),
                                b(5), own));
    }
    if (f == "seriesOpacity") {
        return number(seriesOpacity(k(0), k(1), k(2), b(3), b(4)));
    }
    FAIL("a question this test cannot ask: " << f);
    return {};
}

} // namespace

TEST_CASE("every question the viewer's parity suite asked has the answer it was given", "[viewer]")
{
    std::ifstream file(PLOTCORE_VIEWER_ANSWERS, std::ios::binary);
    REQUIRE(file.good());
    std::vector<LineColours> colours;
    std::map<std::string, int> asked;
    std::string line;
    int failures = 0;
    std::string failed;
    int lines = 0;
    while (std::getline(file, line)) {
        if (line.empty()) {
            continue;
        }
        ++lines;
        const std::optional<Value> record = json::parse(line);
        if (!record.has_value()) {
            FAIL("line " << lines << " is not JSON");
        }
        const std::string f = record->textOr("f", "");
        const Value& a = *record->find("a");
        if (f == "colours") {
            for (const Value& one : a.items) {
                colours.push_back(coloursOf(one));
            }
            continue;
        }
        ++asked[f];
        std::string why;
        if (!same(*record->find("r"), answer(f, a.items, colours), why)) {
            ++failures;
            if (failures <= 20) {
                failed += f + json::write(a) + ": " + why + "\n";
            }
        }
    }
    INFO(failed);
    // Every kind of question the suite asks, asked.
    REQUIRE(asked.size() == 26);
    REQUIRE(asked["seriesColor"] > 1000);
    CHECK(failures == 0);
}
