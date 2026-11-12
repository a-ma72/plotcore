// SPDX-FileCopyrightText: 2026 Jonas Sattler
// SPDX-License-Identifier: BSD-3-Clause

#include "plotcore/qt/PlotWidget.hpp"

#include <QCursor>
#include <QEnterEvent>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRegion>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace plotcore::qt {
namespace {

QColor qcolor(const Rgba& colour)
{
    return QColor::fromRgbF(static_cast<float>(colour.red), static_cast<float>(colour.green),
                            static_cast<float>(colour.blue), static_cast<float>(colour.alpha));
}

QPointF qpoint(const Point& point)
{
    return {point.x(), point.y()};
}

Modifiers modifiersOf(Qt::KeyboardModifiers keys)
{
    // The event carries the modifiers Qt last heard. A Shift held down before
    // this widget had the keyboard is not among them, so a gesture would
    // ignore it until a click gave the plot the focus. The platform is asked
    // as well, which is the keys actually down.
    keys |= QGuiApplication::queryKeyboardModifiers();
    return Modifiers{keys.testFlag(Qt::ShiftModifier), keys.testFlag(Qt::ControlModifier),
                     keys.testFlag(Qt::AltModifier)};
}

std::optional<Button> buttonOf(Qt::MouseButton button)
{
    switch (button) {
    case Qt::LeftButton:
        return Button::Left;
    case Qt::MiddleButton:
        return Button::Middle;
    case Qt::RightButton:
        return Button::Right;
    default:
        return std::nullopt;
    }
}

std::size_t slot(TextRole role)
{
    return static_cast<std::size_t>(role);
}

} // namespace

PlotWidget::PlotWidget(QWidget* parent) : QWidget(parent)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::WheelFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMinimumSize(160, 120);
    applyMeasure();
}

void PlotWidget::setRoleFont(TextRole role, const QFont& font)
{
    fonts_[slot(role)] = font;
    fontSet_[slot(role)] = true;
    applyMeasure();
    update();
}

QFont PlotWidget::roleFont(TextRole role) const
{
    if (fontSet_[slot(role)]) {
        return fonts_[slot(role)];
    }
    QFont font = this->font();
    font.setPixelSize(static_cast<int>(std::lround(figure_.style().pixelsOf(role))));
    if (role == TextRole::Title) {
        font.setWeight(QFont::DemiBold);
    }
    return font;
}

void PlotWidget::applyMeasure()
{
    // Read the fonts when asked rather than captured now, so a font changed
    // later is the one the next frame is laid out in.
    TextMeasure measure;
    measure.width = [this](const std::string& text, TextRole role) {
        return QFontMetricsF(roleFont(role)).horizontalAdvance(QString::fromStdString(text));
    };
    measure.height = [this](TextRole role) { return QFontMetricsF(roleFont(role)).height(); };
    figure_.setMeasure(std::move(measure));
}

void PlotWidget::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::FontChange) {
        update();
    }
    QWidget::changeEvent(event);
}

void PlotWidget::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    // A window that opens under the pointer never gets an enter. The plot
    // takes the keyboard then, so a dock's first box does not keep it.
    if (!tookFocus_) {
        tookFocus_ = true;
        setFocus(Qt::OtherFocusReason);
    }
}

void PlotWidget::enterEvent(QEnterEvent* event)
{
    // Shift and Ctrl are this plot's gestures. They arrive only at the widget
    // that has the keyboard, so the pointer arriving is enough and a click is
    // not required first.
    setFocus(Qt::MouseFocusReason);
    QWidget::enterEvent(event);
}

void PlotWidget::followModifiers()
{
    if (QGuiApplication::mouseButtons() == Qt::NoButton) {
        return;
    }
    const QPoint at = mapFromGlobal(QCursor::pos());
    if (!rect().contains(at)) {
        return;
    }
    figure_.move(at.x(), at.y(), modifiersOf(QGuiApplication::queryKeyboardModifiers()));
    update();
}

void PlotWidget::keyPressEvent(QKeyEvent* event)
{
    const int key = event->key();
    if (!event->isAutoRepeat() &&
        (key == Qt::Key_Shift || key == Qt::Key_Control || key == Qt::Key_Alt)) {
        followModifiers();
    }
    QWidget::keyPressEvent(event);
}

void PlotWidget::keyReleaseEvent(QKeyEvent* event)
{
    const int key = event->key();
    if (!event->isAutoRepeat() &&
        (key == Qt::Key_Shift || key == Qt::Key_Control || key == Qt::Key_Alt)) {
        followModifiers();
    }
    QWidget::keyReleaseEvent(event);
}

void PlotWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    update();
}

void PlotWidget::paintEvent(QPaintEvent*)
{
    figure_.setSize(width(), height(), devicePixelRatioF());
    scene_ = figure_.scene();
    QPainter painter(this);
    // The grid is a handful of marks. The lines are what a pan can copy:
    // whole device pixels slide the image, and only the strip that entered
    // is stroked.
    paintChrome(painter, scene_);
    if (!scrollLines(scene_)) {
        storeLines(scene_);
    }
    painter.drawImage(QPointF(0, 0), lines_);
    const QRectF area(scene_.area.x, scene_.area.y, scene_.area.width, scene_.area.height);
    for (const Mark& mark : scene_.over) {
        paintMark(painter, mark, area);
    }
}

QImage PlotWidget::render(QSize size, double scale)
{
    const double width = std::max(1, size.width());
    const double height = std::max(1, size.height());
    QImage image(static_cast<int>(std::lround(width * scale)),
                 static_cast<int>(std::lround(height * scale)),
                 QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(scale);
    image.fill(Qt::transparent);
    // Laid out at the size asked for, as a picture: the pane goes on being the
    // pane, and the lines are asked for nothing on the picture's account.
    SceneRequest request;
    request.picture = true;
    request.width = width;
    request.height = height;
    request.pixelRatio = scale;
    const Scene picture = figure_.scene(request);
    {
        QPainter painter(&image);
        paintScene(painter, picture);
    }
    return image;
}

void PlotWidget::drawText(QPainter& painter, const TextItem& text) const
{
    const QFont font = roleFont(text.role);
    const QFont raised = roleFont(TextRole::TickMinor);
    const QFontMetricsF metrics(font);
    const QFontMetricsF raisedMetrics(raised);
    const QString base = QString::fromStdString(text.base.empty() ? text.text : text.base);
    const QString exponent = QString::fromStdString(text.exponent);
    const double baseWidth = metrics.horizontalAdvance(base);
    const double width =
        baseWidth + (exponent.isEmpty() ? 0.0 : raisedMetrics.horizontalAdvance(exponent));
    const double height = metrics.height();

    const auto offset = [](TextItem::Align align, double extent) {
        switch (align) {
        case TextItem::Align::Middle:
            return -extent / 2.0;
        case TextItem::Align::End:
            return -extent;
        case TextItem::Align::Start:
            break;
        }
        return 0.0;
    };

    painter.save();
    painter.translate(qpoint(text.at));
    painter.rotate(text.turn);
    const double left = offset(text.across, width);
    const double top = offset(text.down, height);
    painter.setPen(qcolor(text.colour));
    painter.setFont(font);
    painter.drawText(QPointF(left, top + metrics.ascent()), base);
    if (!exponent.isEmpty()) {
        painter.setFont(raised);
        painter.drawText(QPointF(left + baseWidth, top + metrics.ascent() - text.raise), exponent);
    }
    painter.restore();
}

void PlotWidget::paintMark(QPainter& painter, const Mark& mark, const QRectF& area) const
{
    painter.save();
    if (mark.clipped) {
        painter.setClipRect(area);
    }
    if (mark.kind == Mark::Kind::Text) {
        painter.setRenderHint(QPainter::Antialiasing, true);
        drawText(painter, mark.text);
        painter.restore();
        return;
    }
    if (mark.width > 0.0) {
        QPen pen(qcolor(mark.stroke), mark.width);
        pen.setCapStyle(Qt::FlatCap);
        if (mark.dashed) {
            pen.setStyle(Qt::DashLine);
        }
        painter.setPen(pen);
    }
    else {
        painter.setPen(Qt::NoPen);
    }
    painter.setBrush(mark.fill.alpha > 0.0 ? QBrush(qcolor(mark.fill)) : QBrush(Qt::NoBrush));
    switch (mark.kind) {
    case Mark::Kind::Segment:
        // Crisp rather than smeared over two rows.
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.drawLine(qpoint(mark.from), qpoint(mark.to));
        break;
    case Mark::Kind::Box: {
        // Filled to its bounds, and edged inside them.
        const QRectF box(qpoint(mark.from), qpoint(mark.to));
        painter.setRenderHint(QPainter::Antialiasing, mark.radius > 0.0);
        const QPen edge = painter.pen();
        painter.setPen(Qt::NoPen);
        painter.drawRoundedRect(box, mark.radius, mark.radius);
        if (mark.width > 0.0) {
            const double half = mark.width / 2.0;
            const double radius = std::max(0.0, mark.radius - half);
            painter.setPen(edge);
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(box.adjusted(half, half, -half, -half), radius, radius);
        }
        break;
    }
    case Mark::Kind::Circle:
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.drawEllipse(qpoint(mark.from), mark.radius, mark.radius);
        break;
    case Mark::Kind::Text:
        break;
    }
    painter.restore();
}

namespace {

bool segmentHits(const QRectF& rect, double x0, double y0, double x1, double y1)
{
    return !(std::max(x0, x1) < rect.left() || std::min(x0, x1) > rect.right() ||
             std::max(y0, y1) < rect.top() || std::min(y0, y1) > rect.bottom());
}

bool samePlace(const Point& a, const Point& b)
{
    return std::abs(a.x() - b.x()) <= 1e-4 && std::abs(a.y() - b.y()) <= 1e-4;
}

std::vector<Point> gathered(const std::vector<std::vector<Point>>& runs)
{
    std::vector<Point> out;
    for (const std::vector<Point>& run : runs) {
        out.insert(out.end(), run.begin(), run.end());
    }
    return out;
}

std::vector<Point> insideOverlap(std::vector<Point> points, double left, double right, double top,
                                 double bottom)
{
    points.erase(std::remove_if(points.begin(), points.end(),
                                [&](const Point& point) {
                                    return !(point.x() > left && point.x() < right &&
                                             point.y() > top && point.y() < bottom);
                                }),
                 points.end());
    std::sort(points.begin(), points.end(), [](const Point& a, const Point& b) {
        return a.x() < b.x() || (a.x() == b.x() && a.y() < b.y());
    });
    return points;
}

} // namespace

void PlotWidget::paintChrome(QPainter& painter, const Scene& scene) const
{
    painter.setRenderHint(QPainter::Antialiasing, true);
    if (scene.ground.alpha > 0.0) {
        painter.fillRect(QRectF(0, 0, scene.width, scene.height), qcolor(scene.ground));
    }
    const QRectF area(scene.area.x, scene.area.y, scene.area.width, scene.area.height);
    for (const Mark& mark : scene.under) {
        paintMark(painter, mark, area);
    }
}

void PlotWidget::paintLines(QPainter& painter, const Scene& scene,
                            const std::vector<QRectF>* strips) const
{
    const QRectF area(scene.area.x, scene.area.y, scene.area.width, scene.area.height);
    painter.save();
    painter.setClipRect(area);
    if (strips != nullptr && !strips->empty()) {
        QRegion region;
        for (const QRectF& rect : *strips) {
            region = region.united(QRegion(rect.toAlignedRect()));
        }
        painter.setClipRegion(region, Qt::IntersectClip);
    }
    painter.setRenderHint(QPainter::Antialiasing, true);
    for (const DrawnLine& line : scene.lines) {
        QPen pen(qcolor(line.colour), line.width);
        pen.setJoinStyle(Qt::RoundJoin);
        pen.setCapStyle(Qt::RoundCap);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        for (const std::vector<Point>& run : line.runs) {
            std::vector<std::vector<Point>> pieces;
            if (strips == nullptr || strips->empty()) {
                pieces.push_back(run);
            }
            else if (!run.empty()) {
                std::vector<char> need(run.size(), 0);
                for (std::size_t i = 0; i < run.size(); ++i) {
                    for (const QRectF& rect : *strips) {
                        if (rect.contains(qpoint(run[i])) ||
                            (i + 1 < run.size() &&
                             segmentHits(rect, run[i].x(), run[i].y(), run[i + 1].x(),
                                         run[i + 1].y()))) {
                            need[i] = 1;
                            if (i + 1 < run.size()) {
                                need[i + 1] = 1;
                            }
                        }
                    }
                }
                std::size_t start = 0;
                bool open = false;
                for (std::size_t i = 0; i < need.size(); ++i) {
                    if (need[i] && !open) {
                        start = i;
                        open = true;
                    }
                    else if (!need[i] && open) {
                        pieces.emplace_back(run.begin() + static_cast<std::ptrdiff_t>(start),
                                            run.begin() + static_cast<std::ptrdiff_t>(i));
                        open = false;
                    }
                }
                if (open) {
                        pieces.emplace_back(run.begin() + static_cast<std::ptrdiff_t>(start),
                                            run.end());
                }
            }
            for (const std::vector<Point>& piece : pieces) {
                if (piece.size() == 1) {
                    painter.drawPoint(qpoint(piece.front()));
                    continue;
                }
                if (piece.empty()) {
                    continue;
                }
                QPolygonF polygon;
                polygon.reserve(static_cast<qsizetype>(piece.size()));
                for (const Point& point : piece) {
                    polygon.append(qpoint(point));
                }
                painter.drawPolyline(polygon);
            }
        }
        if (!line.markers.empty()) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(qcolor(line.colour));
            for (const Point& point : line.markers) {
                if (strips != nullptr &&
                    std::none_of(strips->begin(), strips->end(), [&](const QRectF& rect) {
                        return rect.contains(qpoint(point));
                    })) {
                    continue;
                }
                painter.drawEllipse(qpoint(point), line.markerRadius, line.markerRadius);
            }
            painter.setBrush(Qt::NoBrush);
        }
    }
    painter.restore();
}

bool PlotWidget::linesMatch(const Scene& scene) const
{
    if (!linesHeld_ || linesScene_.width != scene.width || linesScene_.height != scene.height ||
        linesScene_.x != scene.x || linesScene_.y != scene.y || linesScene_.area.x != scene.area.x ||
        linesScene_.area.y != scene.area.y || linesScene_.area.width != scene.area.width ||
        linesScene_.area.height != scene.area.height ||
        linesScene_.lines.size() != scene.lines.size()) {
        return false;
    }
    for (std::size_t i = 0; i < scene.lines.size(); ++i) {
        const DrawnLine& old = linesScene_.lines[i];
        const DrawnLine& now = scene.lines[i];
        if (old.colour != now.colour || old.width != now.width ||
            old.markerRadius != now.markerRadius || old.runs.size() != now.runs.size() ||
            old.markers.size() != now.markers.size()) {
            return false;
        }
        for (std::size_t run = 0; run < old.runs.size(); ++run) {
            if (old.runs[run].size() != now.runs[run].size()) {
                return false;
            }
            for (std::size_t point = 0; point < old.runs[run].size(); ++point) {
                if (!samePlace(old.runs[run][point], now.runs[run][point])) {
                    return false;
                }
            }
        }
    }
    return true;
}

bool PlotWidget::linesTranslated(const Scene& scene, double sx, double sy) const
{
    if (linesScene_.lines.size() != scene.lines.size()) {
        return false;
    }
    const double left = scene.area.x + std::max(sx, 0.0) + 0.25;
    const double right = scene.area.x + scene.area.width + std::min(sx, 0.0) - 0.25;
    const double top = scene.area.y + std::max(sy, 0.0) + 0.25;
    const double bottom = scene.area.y + scene.area.height + std::min(sy, 0.0) - 0.25;
    if (right - left < 1.0 || bottom - top < 1.0) {
        return false;
    }
    for (std::size_t i = 0; i < scene.lines.size(); ++i) {
        const DrawnLine& old = linesScene_.lines[i];
        const DrawnLine& now = scene.lines[i];
        if (old.colour != now.colour || old.width != now.width ||
            old.markerRadius != now.markerRadius) {
            return false;
        }
        const auto agree = [&](std::vector<Point> was, const std::vector<Point>& is) {
            for (Point& point : was) {
                point = Point(point.x() + sx, point.y() + sy);
            }
            const std::vector<Point> kept = insideOverlap(std::move(was), left, right, top, bottom);
            const std::vector<Point> have = insideOverlap(is, left, right, top, bottom);
            if (kept.size() != have.size()) {
                return false;
            }
            for (std::size_t p = 0; p < kept.size(); ++p) {
                if (!samePlace(kept[p], have[p])) {
                    return false;
                }
            }
            return true;
        };
        if (!agree(gathered(old.runs), gathered(now.runs)) ||
            !agree(old.markers, now.markers)) {
            return false;
        }
    }
    return true;
}

bool PlotWidget::scrollLines(const Scene& scene)
{
    if (!linesHeld_ || lines_.isNull() || linesRatio_ != devicePixelRatioF() ||
        figure_.settings().flipped) {
        return false;
    }
    if (linesScene_.width != scene.width || linesScene_.height != scene.height ||
        linesScene_.area.width != scene.area.width || linesScene_.area.height != scene.area.height ||
        linesScene_.area.x != scene.area.x || linesScene_.area.y != scene.area.y) {
        return false;
    }
    const double xSpan = scene.x.high - scene.x.low;
    const double ySpan = scene.y.high - scene.y.low;
    const double oldX = linesScene_.x.high - linesScene_.x.low;
    const double oldY = linesScene_.y.high - linesScene_.y.low;
    if (!(xSpan > 0.0) || !(ySpan > 0.0)) {
        return false;
    }
    if (std::abs(oldX - xSpan) > 1e-9 * std::max(1.0, std::abs(xSpan)) ||
        std::abs(oldY - ySpan) > 1e-9 * std::max(1.0, std::abs(ySpan))) {
        return false;
    }
    const double sx = (linesScene_.x.low - scene.x.low) / xSpan * scene.area.width;
    const double sy = (scene.y.low - linesScene_.y.low) / ySpan * scene.area.height;
    const double sxd = sx * linesRatio_;
    const double syd = sy * linesRatio_;
    const int rx = static_cast<int>(std::lround(sxd));
    const int ry = static_cast<int>(std::lround(syd));
    if (std::abs(sxd - rx) > 1e-3 || std::abs(syd - ry) > 1e-3 || std::abs(rx) >= lines_.width() ||
        std::abs(ry) >= lines_.height()) {
        return false;
    }
    if (rx == 0 && ry == 0) {
        return linesMatch(scene);
    }
    if (!linesTranslated(scene, sx, sy)) {
        return false;
    }

    QImage moved(lines_.size(), QImage::Format_ARGB32_Premultiplied);
    moved.fill(Qt::transparent);
    QImage source = lines_;
    source.setDevicePixelRatio(1.0);
    const QRectF pane(scene.area.x * linesRatio_, scene.area.y * linesRatio_,
                      scene.area.width * linesRatio_, scene.area.height * linesRatio_);
    const int seam = std::max(2, static_cast<int>(std::ceil(linesRatio_)));
    std::vector<QRectF> device;
    if (rx > 0) {
        device.emplace_back(pane.x(), pane.y(), rx + seam, pane.height());
    }
    else if (rx < 0) {
        device.emplace_back(pane.right() + rx - seam, pane.y(), -rx + seam, pane.height());
    }
    if (ry > 0) {
        device.emplace_back(pane.x(), pane.y(), pane.width(), ry + seam);
    }
    else if (ry < 0) {
        device.emplace_back(pane.x(), pane.bottom() + ry - seam, pane.width(), -ry + seam);
    }
    {
        QPainter painter(&moved);
        // Whole pixels strictly inside the pane. The shifted rectangle hangs a
        // column into the gutter, over the axis, and a fraction off a pixel
        // makes the copy a scale.
        const QRect panePx(static_cast<int>(std::ceil(pane.left() - 1e-6)),
                           static_cast<int>(std::ceil(pane.top() - 1e-6)),
                           static_cast<int>(std::floor(pane.right() + 1e-6) -
                                            std::ceil(pane.left() - 1e-6)),
                           static_cast<int>(std::floor(pane.bottom() + 1e-6) -
                                            std::ceil(pane.top() - 1e-6)));
        const QRect src =
            panePx.adjusted(std::max(-rx, 0), std::max(-ry, 0), -std::max(rx, 0), -std::max(ry, 0));
        const QRect dst = panePx.adjusted(std::max(rx, 0), std::max(ry, 0), -std::max(-rx, 0),
                                          -std::max(-ry, 0));
        if (src.size() != dst.size() || src.width() <= 0 || src.height() <= 0) {
            return false;
        }
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
        painter.setClipRect(panePx);
        painter.drawImage(dst, source, src);
        painter.setClipping(false);
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        for (const QRectF& rect : device) {
            painter.fillRect(rect, Qt::transparent);
        }
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        painter.scale(linesRatio_, linesRatio_);
        std::vector<QRectF> logical;
        logical.reserve(device.size());
        for (const QRectF& rect : device) {
            logical.emplace_back(rect.x() / linesRatio_, rect.y() / linesRatio_,
                                 rect.width() / linesRatio_, rect.height() / linesRatio_);
        }
        paintLines(painter, scene, &logical);
    }
    moved.setDevicePixelRatio(linesRatio_);
    lines_ = moved;
    linesScene_ = scene;
    return true;
}

void PlotWidget::storeLines(const Scene& scene)
{
    const double ratio = devicePixelRatioF();
    QImage image(std::max(1, static_cast<int>(std::lround(scene.width * ratio))),
                 std::max(1, static_cast<int>(std::lround(scene.height * ratio))),
                 QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    {
        QPainter painter(&image);
        painter.scale(ratio, ratio);
        paintLines(painter, scene);
    }
    image.setDevicePixelRatio(ratio);
    lines_ = image;
    linesScene_ = scene;
    linesRatio_ = ratio;
    linesHeld_ = true;
}

void PlotWidget::paintScene(QPainter& painter, const Scene& scene) const
{
    paintChrome(painter, scene);
    paintLines(painter, scene);
    const QRectF area(scene.area.x, scene.area.y, scene.area.width, scene.area.height);
    for (const Mark& mark : scene.over) {
        paintMark(painter, mark, area);
    }
}

void PlotWidget::wheelEvent(QWheelEvent* event)
{
    const QPointF at = event->position();
    // Qt reports a vertical wheel with Alt held as a horizontal one.
    const QPoint delta = event->angleDelta();
    const double angle = delta.y() != 0 ? delta.y() : delta.x();
    figure_.wheel(at.x(), at.y(), angle, modifiersOf(event->modifiers()));
    event->accept();
    update();
    emit changed();
}

void PlotWidget::mousePressEvent(QMouseEvent* event)
{
    const std::optional<Button> button = buttonOf(event->button());
    if (!button.has_value()) {
        QWidget::mousePressEvent(event);
        return;
    }
    const QPointF at = event->position();
    setFocus(Qt::MouseFocusReason);
    figure_.press(at.x(), at.y(), *button, modifiersOf(event->modifiers()));
    event->accept();
    update();
}

void PlotWidget::mouseMoveEvent(QMouseEvent* event)
{
    const QPointF at = event->position();
    figure_.move(at.x(), at.y(), modifiersOf(event->modifiers()));
    event->accept();
    update();
    if (event->buttons() != Qt::NoButton) {
        emit changed();
    }
}

void PlotWidget::mouseReleaseEvent(QMouseEvent* event)
{
    const std::optional<Button> button = buttonOf(event->button());
    if (!button.has_value()) {
        QWidget::mouseReleaseEvent(event);
        return;
    }
    const QPointF at = event->position();
    figure_.release(at.x(), at.y(), *button, modifiersOf(event->modifiers()));
    event->accept();
    update();
    emit changed();
}

void PlotWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    const std::optional<Button> button = buttonOf(event->button());
    if (!button.has_value()) {
        QWidget::mouseDoubleClickEvent(event);
        return;
    }
    const QPointF at = event->position();
    figure_.doubleClick(at.x(), at.y(), *button);
    event->accept();
    update();
    emit changed();
}

void PlotWidget::leaveEvent(QEvent* event)
{
    figure_.leave();
    update();
    QWidget::leaveEvent(event);
}

} // namespace plotcore::qt
