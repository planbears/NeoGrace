#include "pageview.h"
#include "i18n.h"

#include "axes.h"
#include "gracetext.h"
#include "ops.h"

#include <algorithm>
#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QScrollBar>
#include <QStyleHints>
#include <QWheelEvent>
#include <cmath>

namespace ng {

static const double kMargin = 28;

static double distToSeg(QPointF p, QPointF a, QPointF b)
{
    const QPointF d = b - a;
    const double l2 = d.x() * d.x() + d.y() * d.y();
    double t = l2 > 0 ? ((p.x() - a.x()) * d.x() + (p.y() - a.y()) * d.y()) / l2 : 0;
    t = qBound(0.0, t, 1.0);
    const QPointF q = a + d * t;
    return std::hypot(p.x() - q.x(), p.y() - q.y());
}

static QColor accent() { return qApp->palette().color(QPalette::Highlight); }

static bool isDark()
{
    return qApp->palette().color(QPalette::Window).lightness() < 128;
}

PageView::PageView(Document *doc, QWidget *parent) : QAbstractScrollArea(parent), m_doc(doc)
{
    setFrameShape(QFrame::NoFrame);
    viewport()->setMouseTracking(true);
    viewport()->setAttribute(Qt::WA_OpaquePaintEvent);
    setFocusPolicy(Qt::StrongFocus);
    viewport()->grabGesture(Qt::PinchGesture);
    m_renderTimer.setSingleShot(true);
    m_renderTimer.setInterval(0);
    connect(&m_renderTimer, &QTimer::timeout, this, [this] {
        render();
        viewport()->update();
    });
    connect(doc, &Document::changed, this, [this] {
        updateScrollBars();
        scheduleRender();
    });
    connect(doc, &Document::selectionChanged, this, [this] { viewport()->update(); });
}

void PageView::scheduleRender()
{
    m_dirty = true;
    m_renderTimer.start();
}

QRectF PageView::pageRect() const
{
    const Project &pr = m_doc->project();
    const double w = pr.pageW * m_zoom, h = pr.pageH * m_zoom;
    const QSize vs = viewport()->size();
    double x = kMargin - horizontalScrollBar()->value();
    double y = kMargin - verticalScrollBar()->value();
    if (w + 2 * kMargin < vs.width())
        x = (vs.width() - w) / 2;
    if (h + 2 * kMargin < vs.height())
        y = (vs.height() - h) / 2;
    return QRectF(std::round(x), std::round(y), w, h);
}

void PageView::updateScrollBars()
{
    const Project &pr = m_doc->project();
    if (m_fit) {
        const QSize vs = viewport()->size();
        const double z = qMin((vs.width() - 2 * kMargin) / pr.pageW, (vs.height() - 2 * kMargin) / pr.pageH);
        if (z > 0.02 && std::fabs(z - m_zoom) > 1e-6) {
            m_zoom = z;
            emit zoomChanged(m_zoom);
        }
    }
    const int cw = int(pr.pageW * m_zoom + 2 * kMargin), ch = int(pr.pageH * m_zoom + 2 * kMargin);
    const QSize vs = viewport()->size();
    horizontalScrollBar()->setRange(0, qMax(0, cw - vs.width()));
    verticalScrollBar()->setRange(0, qMax(0, ch - vs.height()));
    horizontalScrollBar()->setPageStep(vs.width());
    verticalScrollBar()->setPageStep(vs.height());
    horizontalScrollBar()->setSingleStep(20);
    verticalScrollBar()->setSingleStep(20);
}

void PageView::setZoom(double z, QPoint anchor)
{
    z = qBound(0.05, z, 40.0);
    if (anchor.x() < 0)
        anchor = QPoint(viewport()->width() / 2, viewport()->height() / 2);
    const QRectF pr0 = pageRect();
    const QPointF rel((anchor.x() - pr0.left()) / m_zoom, (anchor.y() - pr0.top()) / m_zoom);
    m_fit = false;
    m_zoom = z;
    updateScrollBars();
    // сохранить точку под курсором
    const Project &pr = m_doc->project();
    const double w = pr.pageW * m_zoom, h = pr.pageH * m_zoom;
    if (w + 2 * kMargin >= viewport()->width())
        horizontalScrollBar()->setValue(int(kMargin + rel.x() * m_zoom - anchor.x()));
    if (h + 2 * kMargin >= viewport()->height())
        verticalScrollBar()->setValue(int(kMargin + rel.y() * m_zoom - anchor.y()));
    emit zoomChanged(m_zoom);
    scheduleRender();
}

void PageView::zoomFit()
{
    m_fit = true;
    updateScrollBars();
    emit zoomChanged(m_zoom);
    scheduleRender();
}

void PageView::setMode(Mode m)
{
    if (m == m_mode)
        return;
    m_mode = m;
    m_drag = None;
    viewport()->setCursor(m == Select ? Qt::ArrowCursor : m == Pan ? Qt::OpenHandCursor : Qt::CrossCursor);
    emit modeChanged(m);
    viewport()->update();
}

void PageView::render()
{
    const double dpr = devicePixelRatioF();
    const QSize vs = viewport()->size();
    if (vs.isEmpty())
        return;
    m_cache = QImage(vs * dpr, QImage::Format_ARGB32_Premultiplied);
    m_cache.setDevicePixelRatio(dpr);
    const int dpm = int(72 / 0.0254);
    m_cache.setDotsPerMeterX(dpm);
    m_cache.setDotsPerMeterY(dpm);
    const QColor bg = isDark() ? QColor(30, 30, 32) : QColor(232, 232, 236);
    m_cache.fill(bg);
    QPainter p(&m_cache);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF page = pageRect();
    // мягкая тень
    for (int i = 1; i <= 6; ++i) {
        QColor c(0, 0, 0, isDark() ? 22 : 10);
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawRoundedRect(page.adjusted(-i, -i + 2, i, i + 2), i, i);
    }
    p.fillRect(page, Qt::white);
    m_info = RenderInfo();
    renderPage(&p, m_doc->project(), page, &m_info);
    p.end();
    m_dirty = false;
}

void PageView::paintEvent(QPaintEvent *)
{
    if (m_dirty || m_cache.isNull() || m_cache.size() != viewport()->size() * devicePixelRatioF())
        render();
    QPainter p(viewport());
    p.drawImage(0, 0, m_cache);
    drawOverlay(p);
}

void PageView::resizeEvent(QResizeEvent *e)
{
    QAbstractScrollArea::resizeEvent(e);
    updateScrollBars();
    scheduleRender();
}

void PageView::scrollContentsBy(int, int)
{
    scheduleRender();
}

void PageView::changeEvent(QEvent *e)
{
    if (e->type() == QEvent::PaletteChange || e->type() == QEvent::StyleChange)
        scheduleRender();
    QAbstractScrollArea::changeEvent(e);
}

// ---------------------------------------------------------------------------
// Поиск под курсором

Sel PageView::hitTest(QPointF pos, int *itemIndex) const
{
    const double tol = 5.0;
    for (int i = m_info.items.size() - 1; i >= 0; --i) {
        const HitItem &h = m_info.items[i];
        bool hit = false;
        switch (h.kind) {
        case HitItem::GraphFrame:
            continue;  // во втором проходе
        case HitItem::Set: {
            if (h.poly.size() == 1) {
                hit = std::hypot(pos.x() - h.poly[0].x(), pos.y() - h.poly[0].y()) < tol + 2;
            } else if (h.polyline) {
                for (int k = 0; k + 1 < h.poly.size() && !hit; ++k)
                    hit = distToSeg(pos, h.poly[k], h.poly[k + 1]) < tol;
            }
            if (!hit && h.points)
                for (const QPointF &q : h.poly)
                    if (std::hypot(pos.x() - q.x(), pos.y() - q.y()) < tol + 2) {
                        hit = true;
                        break;
                    }
            if (!hit && !h.polyline && !h.points)
                hit = h.poly.containsPoint(pos, Qt::OddEvenFill);
            // бары и круговые — по площади
            if (!hit && h.poly.size() == 4 && !h.polyline && !h.points)
                hit = h.poly.boundingRect().contains(pos);
            break;
        }
        case HitItem::Object:
            if (h.polyline && h.poly.size() == 2)
                hit = distToSeg(pos, h.poly[0], h.poly[1]) < tol;
            else
                hit = h.poly.boundingRect().adjusted(-2, -2, 2, 2).contains(pos);
            break;
        default:
            hit = h.poly.boundingRect().adjusted(-2, -2, 2, 2).contains(pos);
        }
        if (!hit)
            continue;
        if (itemIndex)
            *itemIndex = i;
        switch (h.kind) {
        case HitItem::Set: return Sel::set(h.gid, h.sid);
        case HitItem::Legend: return Sel::legend(h.gid);
        case HitItem::Title:
        case HitItem::Subtitle: return Sel::title(h.gid);
        case HitItem::AxisLabel:
        case HitItem::TickLabels: return Sel::axisOf(h.gid, h.axis);
        case HitItem::Object: return Sel::object(h.obj);
        default: break;
        }
    }
    // рамки графиков: верхний (последний) первым
    for (int i = m_info.items.size() - 1; i >= 0; --i) {
        const HitItem &h = m_info.items[i];
        if (h.kind == HitItem::GraphFrame && h.poly.boundingRect().contains(pos)) {
            if (itemIndex)
                *itemIndex = i;
            return Sel::graph(h.gid);
        }
    }
    if (pageRect().contains(pos))
        return Sel::page();
    return Sel();
}

int PageView::graphAt(QPointF pos) const
{
    for (int i = m_info.items.size() - 1; i >= 0; --i) {
        const HitItem &h = m_info.items[i];
        if (h.kind == HitItem::GraphFrame && h.poly.boundingRect().contains(pos))
            return h.gid;
    }
    return -1;
}

static QVector<QPointF> handlePoints(const QRectF &r)
{
    return {r.topLeft(), r.topRight(), r.bottomRight(), r.bottomLeft(),
            QPointF(r.center().x(), r.top()), QPointF(r.right(), r.center().y()),
            QPointF(r.center().x(), r.bottom()), QPointF(r.left(), r.center().y())};
}

int PageView::handleAt(QPointF pos) const
{
    const Sel &s = m_doc->selection();
    if (s.kind != Sel::Graph || !m_info.frames.contains(s.gid))
        return -1;
    const auto pts = handlePoints(m_info.frames.value(s.gid));
    for (int i = 0; i < pts.size(); ++i)
        if (std::fabs(pos.x() - pts[i].x()) <= 6 && std::fabs(pos.y() - pts[i].y()) <= 6)
            return i;
    return -1;
}

Qt::CursorShape PageView::cursorFor(QPointF pos) const
{
    if (m_mode == Pan)
        return m_drag == PanWorld ? Qt::ClosedHandCursor : Qt::OpenHandCursor;
    if (m_mode != Select)
        return Qt::CrossCursor;
    const int h = handleAt(pos);
    if (h >= 0) {
        static const Qt::CursorShape cs[] = {Qt::SizeFDiagCursor, Qt::SizeBDiagCursor, Qt::SizeFDiagCursor,
                                             Qt::SizeBDiagCursor, Qt::SizeVerCursor, Qt::SizeHorCursor,
                                             Qt::SizeVerCursor, Qt::SizeHorCursor};
        return cs[h];
    }
    const Sel s = hitTest(pos);
    if (s.kind == Sel::Legend || s.kind == Sel::Object)
        return Qt::SizeAllCursor;
    if (s.kind == Sel::Graph && m_doc->selection() == s)
        return Qt::SizeAllCursor;
    if (s.kind == Sel::Set)
        return Qt::PointingHandCursor;
    return Qt::ArrowCursor;
}

// ---------------------------------------------------------------------------
// Мышь

void PageView::mousePressEvent(QMouseEvent *e)
{
    setFocus();
    const QPointF pos = e->position();
    m_press = m_last = pos;
    m_moved = false;
    m_dragStart = m_doc->project();
    if (e->button() == Qt::MiddleButton) {
        m_drag = ScrollPage;
        viewport()->setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (e->button() == Qt::RightButton) {
        Sel s = hitTest(pos);
        const bool inGroup = s.kind == Sel::Set && m_doc->isSetSelected(s.gid, s.sid);
        if (s.kind != Sel::None && s.kind != Sel::Page && !inGroup)
            m_doc->select(s);
        emit contextMenuWanted(s, e->globalPosition().toPoint());
        return;
    }
    if (e->button() != Qt::LeftButton)
        return;
    switch (m_mode) {
    case Select: {
        m_handle = handleAt(pos);
        if (m_handle >= 0) {
            m_drag = ResizeGraph;
            m_dragGid = m_doc->selection().gid;
            return;
        }
        const Sel prev = m_doc->selection();
        Sel s = hitTest(pos);
        m_drag = None;
        // ⌘/⇧-щелчок по набору — добавить к выделению или убрать из него
        if (s.kind == Sel::Set && (e->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::MetaModifier))) {
            if (prev.kind == Sel::Set || !m_doc->selectedSets().isEmpty())
                m_doc->toggleSet(s);
            else
                m_doc->select(s);
            return;
        }
        if (s.kind == Sel::Legend) {
            m_drag = MoveLegend;
            m_dragGid = s.gid;
        } else if (s.kind == Sel::Object) {
            m_drag = MoveObject;
        } else if (s.kind == Sel::Graph && prev == s) {
            m_drag = MoveGraph;
            m_dragGid = s.gid;
        }
        m_doc->select(s.kind == Sel::None ? Sel() : s);
        break;
    }
    case Zoom:
        m_dragGid = graphAt(pos);
        if (m_dragGid >= 0) {
            m_drag = RubberZoom;
            m_doc->select(Sel::graph(m_dragGid));
        }
        break;
    case Pan:
        m_dragGid = graphAt(pos);
        m_drag = m_dragGid >= 0 ? PanWorld : ScrollPage;
        viewport()->setCursor(Qt::ClosedHandCursor);
        break;
    default:
        m_drag = Create;
        break;
    }
}

void PageView::mouseMoveEvent(QMouseEvent *e)
{
    const QPointF pos = e->position();
    // строка состояния: координаты
    {
        const int gid = graphAt(pos);
        QString txt;
        if (gid >= 0 && m_info.maps.contains(gid)) {
            const WorldMap &wm = m_info.maps[gid];
            const QPointF v = toView(pos);
            txt = QString("G%1:  x = %2   y = %3").arg(gid).arg(QString::number(wm.xw(v.x()), 'g', 6),
                                                          QString::number(wm.yw(v.y()), 'g', 6));
        } else if (pageRect().contains(pos)) {
            const QPointF v = toView(pos);
            txt = QString(T("вьюпорт:  %1, %2")).arg(QString::number(v.x(), 'f', 3), QString::number(v.y(), 'f', 3));
        }
        emit cursorInfo(txt);
    }
    if (m_drag == None) {
        if (m_mode == Select) {
            Sel h = hitTest(pos);
            if (h != m_hover) {
                m_hover = h;
                viewport()->update();
            }
        }
        viewport()->setCursor(cursorFor(pos));
        return;
    }
    if (!m_moved && (pos - m_press).manhattanLength() < QApplication::startDragDistance())
        return;
    m_moved = true;
    const QPointF d = pos - m_last;
    const QPointF dv = toView(pos) - toView(m_press);  // смещение во вьюпорте от начала
    m_last = pos;

    switch (m_drag) {
    case ScrollPage:
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - int(d.x()));
        verticalScrollBar()->setValue(verticalScrollBar()->value() - int(d.y()));
        break;
    case MoveLegend: {
        const int gid = m_dragGid;
        const Graph *g0 = m_dragStart.graph(gid);
        if (!g0)
            break;
        Vec2 p0 = g0->p.v2("legend_pos");
        Vec2 np{p0[0] + dv.x(), p0[1] + dv.y()};
        if (g0->p.s("legend_loctype") == "world") {
            WorldMap wm(*g0);
            QPointF v = wm.toView(p0[0], p0[1]) + dv;
            np = {wm.xw(v.x()), wm.yw(v.y())};
        }
        m_doc->modify(T("Перемещение легенды"), [&](Project &pr) {
            if (Graph *g = pr.graph(gid))
                g->p.setV2("legend_pos", np[0], np[1]);
        }, "drag-legend");
        break;
    }
    case MoveObject: {
        const int oi = m_doc->selection().obj;
        if (oi < 0 || oi >= m_dragStart.objects.size())
            break;
        const DrawObject &o0 = m_dragStart.objects[oi];
        const Graph *g0 = m_dragStart.graph(o0.graph);
        const bool world = o0.p.s("loctype") == "world" && g0;
        auto shift = [&](double x, double y) -> QPointF {
            if (!world)
                return QPointF(x + dv.x(), y + dv.y());
            WorldMap wm(*g0);
            QPointF v = wm.toView(x, y) + dv;
            return QPointF(wm.xw(v.x()), wm.yw(v.y()));
        };
        m_doc->modify(T("Перемещение объекта"), [&](Project &pr) {
            DrawObject &o = pr.objects[oi];
            if (o.kind == "string") {
                Vec2 p = o0.p.v2("pos");
                QPointF n = shift(p[0], p[1]);
                o.p.setV2("pos", n.x(), n.y());
            } else {
                Vec4 c = o0.p.v4("coords");
                QPointF a = shift(c[0], c[1]), b = shift(c[2], c[3]);
                o.p.setV4("coords", a.x(), a.y(), b.x(), b.y());
            }
        }, "drag-object");
        break;
    }
    case MoveGraph:
    case ResizeGraph: {
        const int gid = m_dragGid;
        const Graph *g0 = m_dragStart.graph(gid);
        if (!g0)
            break;
        Vec4 v = g0->p.v4("view");
        double x1 = qMin(v[0], v[2]), x2 = qMax(v[0], v[2]), y1 = qMin(v[1], v[3]), y2 = qMax(v[1], v[3]);
        if (m_drag == MoveGraph) {
            x1 += dv.x(); x2 += dv.x(); y1 += dv.y(); y2 += dv.y();
        } else {
            const int h = m_handle;
            if (h == 0 || h == 3 || h == 7) x1 += dv.x();
            if (h == 1 || h == 2 || h == 5) x2 += dv.x();
            if (h == 0 || h == 1 || h == 4) y2 += dv.y();
            if (h == 2 || h == 3 || h == 6) y1 += dv.y();
            if (x2 - x1 < 0.05) { if (h == 0 || h == 3 || h == 7) x1 = x2 - 0.05; else x2 = x1 + 0.05; }
            if (y2 - y1 < 0.05) { if (h == 2 || h == 3 || h == 6) y1 = y2 - 0.05; else y2 = y1 + 0.05; }
        }
        const bool moveLegend = m_drag == MoveGraph && g0->p.s("legend_loctype") == "view";
        const Vec2 lp = g0->p.v2("legend_pos");
        m_doc->modify(m_drag == MoveGraph ? T("Перемещение графика") : T("Размер графика"), [&](Project &pr) {
            if (Graph *g = pr.graph(gid)) {
                g->p.setV4("view", x1, y1, x2, y2);
                if (moveLegend)
                    g->p.setV2("legend_pos", lp[0] + dv.x(), lp[1] + dv.y());
            }
        }, "drag-graph");
        break;
    }
    case PanWorld: {
        const int gid = m_dragGid;
        const Graph *g0 = m_dragStart.graph(gid);
        if (!g0)
            break;
        WorldMap wm(*g0);
        const QPointF a = toView(m_press), b = toView(pos);
        const double dfx = fscale(wm.xw(a.x()), wm.xs) - fscale(wm.xw(b.x()), wm.xs);
        const double dfy = fscale(wm.yw(a.y()), wm.ys) - fscale(wm.yw(b.y()), wm.ys);
        Vec4 w = g0->p.v4("world");
        const double nx1 = ifscale(fscale(w[0], wm.xs) + dfx, wm.xs), nx2 = ifscale(fscale(w[2], wm.xs) + dfx, wm.xs);
        const double ny1 = ifscale(fscale(w[1], wm.ys) + dfy, wm.ys), ny2 = ifscale(fscale(w[3], wm.ys) + dfy, wm.ys);
        if (!std::isfinite(nx1 + nx2 + ny1 + ny2))
            break;
        m_doc->modify(T("Сдвиг"), [&](Project &pr) {
            if (Graph *g = pr.graph(gid))
                g->p.setV4("world", nx1, ny1, nx2, ny2);
        }, "pan-world");
        break;
    }
    case RubberZoom:
    case Create:
        viewport()->update();
        break;
    default:
        break;
    }
}

void PageView::mouseReleaseEvent(QMouseEvent *e)
{
    const QPointF pos = e->position();
    const Drag d = m_drag;
    m_drag = None;
    if (d == RubberZoom) {
        const int gid = m_dragGid;
        const Graph *g = m_doc->project().graph(gid);
        if (g && m_info.maps.contains(gid)) {
            const WorldMap &wm = m_info.maps[gid];
            if (m_moved) {
                QRectF r = QRectF(m_press, pos).normalized();
                if (r.width() > 4 && r.height() > 4) {
                    QPointF a = toView(r.bottomLeft()), b = toView(r.topRight());
                    double x1 = wm.xw(a.x()), x2 = wm.xw(b.x()), y1 = wm.yw(a.y()), y2 = wm.yw(b.y());
                    if (x1 > x2) std::swap(x1, x2);
                    if (y1 > y2) std::swap(y1, y2);
                    m_doc->modify(T("Увеличение"), [&](Project &pr) {
                        if (Graph *gg = pr.graph(gid)) {
                            gg->p.setV4("world", x1, y1, x2, y2);
                            updateTicksForWorld(*gg);
                        }
                    });
                }
            } else {
                // щелчок: ×2 вокруг точки, с Alt/Option — уменьшение
                const double f = (e->modifiers() & Qt::AltModifier) ? 2.0 : 0.5;
                const QPointF c = toView(pos);
                const double cx = fscale(wm.xw(c.x()), wm.xs), cy = fscale(wm.yw(c.y()), wm.ys);
                const double hx = (wm.fx2 - wm.fx1) * f / 2, hy = (wm.fy2 - wm.fy1) * f / 2;
                const double x1 = ifscale(cx - hx, wm.xs), x2 = ifscale(cx + hx, wm.xs);
                const double y1 = ifscale(cy - hy, wm.ys), y2 = ifscale(cy + hy, wm.ys);
                m_doc->modify(f < 1 ? T("Увеличение") : T("Уменьшение"), [&](Project &pr) {
                    if (Graph *gg = pr.graph(gid)) {
                        gg->p.setV4("world", x1, y1, x2, y2);
                        updateTicksForWorld(*gg);
                    }
                });
            }
        }
        viewport()->update();
    } else if (d == PanWorld) {
        if (Graph *g = const_cast<Graph *>(m_doc->project().graph(m_dragGid))) {
            Q_UNUSED(g);
            const int gid = m_dragGid;
            m_doc->modify(T("Сдвиг"), [&](Project &pr) {
                if (Graph *gg = pr.graph(gid))
                    updateTicksForWorld(*gg);
            }, "pan-world");
        }
    } else if (d == Create) {
        finishCreate(m_press, pos);
        viewport()->update();
    }
    viewport()->setCursor(cursorFor(pos));
}

void PageView::finishCreate(QPointF a, QPointF b)
{
    const QPointF va = toView(a), vb = toView(b);
    const bool click = (a - b).manhattanLength() < 4;
    int newIndex = -1;
    m_doc->modify(T("Новый объект"), [&](Project &pr) {
        if (m_mode == AddText) {
            DrawObject o("string");
            o.p.setV2("pos", va.x(), va.y());
            o.p.set("text", T("Текст"));
            o.p.set("font", pr.graphs.isEmpty() ? 0 : pr.graphs.first().p.i("title_font"));
            pr.objects.append(o);
        } else if (m_mode == AddLine) {
            DrawObject o("line");
            QPointF e = click ? va + QPointF(0.15, 0) : vb;
            o.p.setV4("coords", va.x(), va.y(), e.x(), e.y());
            o.p.set("arrow", 2);
            o.p.set("arrow_type", 1);
            pr.objects.append(o);
        } else {
            DrawObject o(m_mode == AddBox ? "box" : "ellipse");
            QPointF e = click ? va + QPointF(0.15, -0.1) : vb;
            o.p.setV4("coords", qMin(va.x(), e.x()), qMin(va.y(), e.y()), qMax(va.x(), e.x()), qMax(va.y(), e.y()));
            pr.objects.append(o);
        }
        newIndex = pr.objects.size() - 1;
    });
    m_doc->select(Sel::object(newIndex));
    const bool wasText = m_mode == AddText;
    setMode(Select);
    if (wasText)
        emit editTextWanted(Sel::object(newIndex));
}

void PageView::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (m_mode != Select || e->button() != Qt::LeftButton)
        return;
    // двойной щелчок — свойства элемента
    Sel s = hitTest(e->position());
    if (s.kind == Sel::None)
        return;
    if (!(s.kind == Sel::Set && m_doc->isSetSelected(s.gid, s.sid)))
        m_doc->select(s);
    emit propertiesWanted(s);
}

void PageView::wheelEvent(QWheelEvent *e)
{
    if (e->modifiers() & Qt::ControlModifier) {
        const double steps = e->angleDelta().y() / 120.0;
        setZoom(m_zoom * std::pow(1.15, steps), e->position().toPoint());
        e->accept();
        return;
    }
    QAbstractScrollArea::wheelEvent(e);
}

bool PageView::viewportEvent(QEvent *e)
{
    if (e->type() == QEvent::NativeGesture) {
        auto *g = static_cast<QNativeGestureEvent *>(e);
        if (g->gestureType() == Qt::ZoomNativeGesture) {
            setZoom(m_zoom * (1.0 + g->value()), g->position().toPoint());
            return true;
        }
        if (g->gestureType() == Qt::SmartZoomNativeGesture) {
            zoomFit();
            return true;
        }
    }
    return QAbstractScrollArea::viewportEvent(e);
}

void PageView::leaveEvent(QEvent *e)
{
    m_hover = Sel();
    emit cursorInfo(QString());
    viewport()->update();
    QAbstractScrollArea::leaveEvent(e);
}

void PageView::keyPressEvent(QKeyEvent *e)
{
    const Sel s = m_doc->selection();
    double dx = 0, dy = 0;
    const double step = (e->modifiers() & Qt::ShiftModifier) ? 0.02 : 0.004;
    switch (e->key()) {
    case Qt::Key_Left: dx = -step; break;
    case Qt::Key_Right: dx = step; break;
    case Qt::Key_Up: dy = step; break;
    case Qt::Key_Down: dy = -step; break;
    case Qt::Key_Escape:
        if (m_mode != Select)
            setMode(Select);
        else
            m_doc->select(Sel());
        return;
    default:
        QAbstractScrollArea::keyPressEvent(e);
        return;
    }
    if (s.kind == Sel::Object) {
        m_doc->modify(T("Сдвиг объекта"), [&](Project &pr) {
            if (s.obj < 0 || s.obj >= pr.objects.size()) return;
            DrawObject &o = pr.objects[s.obj];
            const Graph *g = pr.graph(o.graph);
            const bool world = o.p.s("loctype") == "world" && g;
            auto sh = [&](double x, double y) {
                if (!world) return QPointF(x + dx, y + dy);
                WorldMap wm(*g);
                QPointF v = wm.toView(x, y) + QPointF(dx, dy);
                return QPointF(wm.xw(v.x()), wm.yw(v.y()));
            };
            if (o.kind == "string") {
                Vec2 p = o.p.v2("pos");
                QPointF n = sh(p[0], p[1]);
                o.p.setV2("pos", n.x(), n.y());
            } else {
                Vec4 c = o.p.v4("coords");
                QPointF a = sh(c[0], c[1]), b = sh(c[2], c[3]);
                o.p.setV4("coords", a.x(), a.y(), b.x(), b.y());
            }
        }, "nudge");
    } else if (s.kind == Sel::Legend || s.kind == Sel::Graph) {
        m_doc->modify(s.kind == Sel::Legend ? T("Сдвиг легенды") : T("Сдвиг графика"), [&](Project &pr) {
            Graph *g = pr.graph(s.gid);
            if (!g) return;
            if (s.kind == Sel::Legend) {
                Vec2 p = g->p.v2("legend_pos");
                if (g->p.s("legend_loctype") == "world") {
                    WorldMap wm(*g);
                    QPointF v = wm.toView(p[0], p[1]) + QPointF(dx, dy);
                    g->p.setV2("legend_pos", wm.xw(v.x()), wm.yw(v.y()));
                } else {
                    g->p.setV2("legend_pos", p[0] + dx, p[1] + dy);
                }
            } else {
                Vec4 v = g->p.v4("view");
                g->p.setV4("view", v[0] + dx, v[1] + dy, v[2] + dx, v[3] + dy);
            }
        }, "nudge");
    } else {
        QAbstractScrollArea::keyPressEvent(e);
    }
}

// ---------------------------------------------------------------------------
// Поверх страницы: выделение, ручки, рамка увеличения

QRectF PageView::selectionRect() const
{
    const Sel &s = m_doc->selection();
    QRectF r;
    for (const HitItem &h : m_info.items) {
        bool match = false;
        switch (s.kind) {
        case Sel::Legend: match = h.kind == HitItem::Legend && h.gid == s.gid; break;
        case Sel::Title: match = (h.kind == HitItem::Title || h.kind == HitItem::Subtitle) && h.gid == s.gid; break;
        case Sel::Axis:
            match = (h.kind == HitItem::AxisLabel || h.kind == HitItem::TickLabels) && h.gid == s.gid && h.axis == s.axis;
            break;
        case Sel::Object: match = h.kind == HitItem::Object && h.obj == s.obj; break;
        default: break;
        }
        if (match)
            r = r.isNull() ? h.poly.boundingRect() : r.united(h.poly.boundingRect());
    }
    return r;
}

void PageView::drawOverlay(QPainter &p)
{
    p.setRenderHint(QPainter::Antialiasing);
    const QColor ac = accent();
    const Sel &s = m_doc->selection();

    auto outlineItems = [&](const Sel &sel, bool strong) {
        QColor c = ac;
        c.setAlphaF(strong ? 0.95 : 0.45);
        for (const HitItem &h : m_info.items) {
            if (sel.kind == Sel::Set && h.kind == HitItem::Set && h.gid == sel.gid && h.sid == sel.sid) {
                QColor halo = ac;
                halo.setAlphaF(strong ? 0.35 : 0.2);
                if (h.polyline && h.poly.size() > 1) {
                    p.setPen(QPen(halo, 7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                    p.drawPolyline(h.poly);
                }
                if (h.points || h.poly.size() == 1) {
                    QColor fillc = ac;
                    fillc.setAlphaF(strong ? 0.22 : 0.12);
                    p.setPen(QPen(c, strong ? 1.5 : 1));
                    p.setBrush(fillc);
                    const int step = qMax(1, int(h.poly.size() / 600));
                    for (int k = 0; k < h.poly.size(); k += step)
                        p.drawEllipse(h.poly[k], 8, 8);
                    p.setBrush(Qt::NoBrush);
                }
                if (!h.polyline && !h.points && h.poly.size() > 2) {
                    p.setPen(QPen(c, 1.5, Qt::DashLine));
                    p.drawPolygon(h.poly);
                }
            }
        }
        if (sel.kind == Sel::Graph && m_info.frames.contains(sel.gid)) {
            const QRectF r = m_info.frames.value(sel.gid);
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(c, strong ? 1.5 : 1, Qt::DashLine));
            p.drawRect(r.adjusted(-3, -3, 3, 3));
            if (strong) {
                p.setPen(QPen(Qt::white, 1));
                p.setBrush(ac);
                for (const QPointF &q : handlePoints(r))
                    p.drawRoundedRect(QRectF(q.x() - 4, q.y() - 4, 8, 8), 2, 2);
                // подпись
                QFont f = font();
                f.setPointSizeF(f.pointSizeF() * 0.85);
                p.setFont(f);
                const QString lbl = QString("G%1").arg(sel.gid);
                QRectF tr(r.left() - 3, r.top() - 22, QFontMetricsF(f).horizontalAdvance(lbl) + 12, 16);
                p.setPen(Qt::NoPen);
                p.setBrush(ac);
                p.drawRoundedRect(tr, 4, 4);
                p.setPen(Qt::white);
                p.drawText(tr, Qt::AlignCenter, lbl);
            }
        }
        if (sel.kind == Sel::Legend || sel.kind == Sel::Title || sel.kind == Sel::Axis || sel.kind == Sel::Object) {
            Sel save = m_doc->selection();
            Q_UNUSED(save);
            for (const HitItem &h : m_info.items) {
                bool match = false;
                switch (sel.kind) {
                case Sel::Legend: match = h.kind == HitItem::Legend && h.gid == sel.gid; break;
                case Sel::Title: match = (h.kind == HitItem::Title || h.kind == HitItem::Subtitle) && h.gid == sel.gid; break;
                case Sel::Axis:
                    match = (h.kind == HitItem::AxisLabel || h.kind == HitItem::TickLabels) && h.gid == sel.gid
                            && h.axis == sel.axis;
                    break;
                case Sel::Object: match = h.kind == HitItem::Object && h.obj == sel.obj; break;
                default: break;
                }
                if (!match)
                    continue;
                p.setBrush(Qt::NoBrush);
                if (h.polyline && h.poly.size() == 2) {
                    QColor halo = ac;
                    halo.setAlphaF(0.35);
                    p.setPen(QPen(halo, 7, Qt::SolidLine, Qt::RoundCap));
                    p.drawLine(h.poly[0], h.poly[1]);
                    if (strong) {
                        p.setPen(QPen(Qt::white, 1));
                        p.setBrush(ac);
                        p.drawEllipse(h.poly[0], 4, 4);
                        p.drawEllipse(h.poly[1], 4, 4);
                    }
                } else {
                    QColor fillc = ac;
                    fillc.setAlphaF(strong ? 0.08 : 0.05);
                    p.setBrush(fillc);
                    p.setPen(QPen(c, strong ? 1.5 : 1));
                    p.drawRoundedRect(h.poly.boundingRect().adjusted(-3, -3, 3, 3), 3, 3);
                }
            }
        }
    };

    if (m_mode == Select && m_hover != s && !m_doc->selectedSets().contains(m_hover) && m_hover.kind != Sel::None && m_hover.kind != Sel::Page)
        outlineItems(m_hover, false);
    for (const Sel &x : m_doc->selectedSets())
        if (x != s)
            outlineItems(x, true);
    if (s.kind != Sel::None && s.kind != Sel::Page)
        outlineItems(s, true);

    if (m_drag == RubberZoom && m_moved) {
        QRectF r = QRectF(m_press, m_last).normalized();
        QColor f = ac;
        f.setAlphaF(0.12);
        p.setBrush(f);
        p.setPen(QPen(ac, 1));
        p.drawRect(r);
    }
    if (m_drag == Create && m_moved) {
        p.setPen(QPen(ac, 1.5, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        if (m_mode == AddLine)
            p.drawLine(m_press, m_last);
        else if (m_mode == AddEllipse)
            p.drawEllipse(QRectF(m_press, m_last).normalized());
        else if (m_mode == AddBox)
            p.drawRect(QRectF(m_press, m_last).normalized());
    }
}

}  // namespace ng
