#include "render.h"

#include "gracetext.h"
#include "patterns.h"

#include <algorithm>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <algorithm>
#include <cmath>
#include <limits>

namespace ng {

static constexpr double FONT_SCALE = 0.028;
static constexpr double LW_SCALE = 0.0015;

double lineWidthPx(double lw, double unit) { return qMax(0.0, lw) * LW_SCALE * unit; }

QVector<qreal> dashPattern(int style)
{
    switch (style) {
    case 2: return {1, 3};
    case 3: return {5, 3};
    case 4: return {7, 3};
    case 5: return {1, 3, 5, 3};
    case 6: return {1, 3, 7, 3};
    case 7: return {1, 3, 5, 3, 1, 3};
    case 8: return {5, 3, 1, 3, 5, 3};
    default: return {};
    }
}

namespace {

inline bool finite2(double a, double b) { return std::isfinite(a) && std::isfinite(b); }

Qt::BrushStyle patternStyle(int pat)
{
    if (pat <= 0)
        return Qt::NoBrush;
    if (pat == 1)
        return Qt::SolidPattern;
    static const Qt::BrushStyle map[] = {
        Qt::Dense1Pattern, Qt::Dense2Pattern, Qt::Dense3Pattern, Qt::Dense4Pattern,
        Qt::Dense5Pattern, Qt::Dense6Pattern, Qt::Dense7Pattern, Qt::HorPattern,
        Qt::VerPattern, Qt::CrossPattern, Qt::BDiagPattern, Qt::FDiagPattern,
        Qt::DiagCrossPattern,
    };
    return map[(pat - 2) % 13];
}

QImage patternImageImpl(int pattern, const QColor &c)
{
    QImage img(16, 16, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    const unsigned char *bits = kGracePatterns[qBound(0, pattern, 31)];
    const QRgb px = c.rgba();
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x)
            if (bits[y * 2 + x / 8] & (1 << (x % 8)))
                img.setPixel(x, y, px);
    return img;
}

// Отсечение отрезка (Лян — Барски); true — есть видимая часть
bool clipSeg(QPointF &a, QPointF &b, const QRectF &r)
{
    double t0 = 0, t1 = 1;
    const double dx = b.x() - a.x(), dy = b.y() - a.y();
    const double p[4] = {-dx, dx, -dy, dy};
    const double q[4] = {a.x() - r.left(), r.right() - a.x(), a.y() - r.top(), r.bottom() - a.y()};
    for (int i = 0; i < 4; ++i) {
        if (p[i] == 0) {
            if (q[i] < 0)
                return false;
        } else {
            double t = q[i] / p[i];
            if (p[i] < 0) {
                if (t > t1) return false;
                if (t > t0) t0 = t;
            } else {
                if (t < t0) return false;
                if (t < t1) t1 = t;
            }
        }
    }
    QPointF na(a.x() + t0 * dx, a.y() + t0 * dy), nb(a.x() + t1 * dx, a.y() + t1 * dy);
    a = na;
    b = nb;
    return true;
}

// Разбить полилинию на куски, обрезав далеко вылезающие участки
QVector<QPolygonF> safePolylines(const QVector<QPointF> &pts, const QRectF &box)
{
    QVector<QPolygonF> out;
    bool allIn = true;
    for (const auto &q : pts)
        if (!box.contains(q)) {
            allIn = false;
            break;
        }
    if (allIn) {
        out.append(QPolygonF(pts));
        return out;
    }
    QPolygonF cur;
    for (int i = 0; i + 1 < pts.size(); ++i) {
        QPointF a = pts[i], b = pts[i + 1];
        if (!clipSeg(a, b, box)) {
            if (cur.size() > 1)
                out.append(cur);
            cur.clear();
            continue;
        }
        if (cur.isEmpty() || cur.last() != a) {
            if (cur.size() > 1)
                out.append(cur);
            cur.clear();
            cur << a;
        }
        cur << b;
        if (b != pts[i + 1]) {
            out.append(cur);
            cur.clear();
        }
    }
    if (cur.size() > 1)
        out.append(cur);
    return out;
}

QPointF clampPt(QPointF p)
{
    const double L = 1e7;
    return QPointF(qBound(-L, p.x(), L), qBound(-L, p.y(), L));
}

class Painter {
public:
    Painter(QPainter *p, const Project &pr, const QRectF &target, RenderInfo *info, const RenderOptions &opt)
        : pr(pr), p(p), page(target), te(pr, p->device()), info(info), opt(opt)
    {
        u = target.width() / pr.pageW * pr.scale();
        if (info) {
            info->page = target;
            info->unit = u;
        }
        big = target.adjusted(-target.width() * 4, -target.height() * 4, target.width() * 4,
                              target.height() * 4);
    }

    const Project &pr;
    QPainter *p;
    QRectF page;
    double u;
    TextEngine te;
    RenderInfo *info;
    RenderOptions opt;
    QRectF big;
    int curGid = -1;
    QRectF extent;

    // ---- координаты
    QPointF dev(double vx, double vy) const { return QPointF(page.left() + vx * u, page.bottom() - vy * u); }
    QPointF dev(QPointF v) const { return dev(v.x(), v.y()); }
    QPointF view(QPointF d) const { return QPointF((d.x() - page.left()) / u, (page.bottom() - d.y()) / u); }

    // ---- стили
    QPen pen(int color, int pattern, double lw, int style, bool data = false) const
    {
        if (pattern == 0 || style == 0)
            return QPen(Qt::NoPen);
        QPen pn(pr.color(color));
        double w = lineWidthPx(lw, u);
        pn.setWidthF(w <= 0 ? 0 : qMax(w, 0.25));
        pn.setCosmetic(false);
        auto dp = dashPattern(style);
        if (!dp.isEmpty()) {
            pn.setDashPattern(dp);
            pn.setCapStyle(Qt::FlatCap);
        } else {
            pn.setCapStyle(data ? Qt::RoundCap : Qt::SquareCap);
        }
        pn.setJoinStyle(data ? Qt::RoundJoin : Qt::MiterJoin);
        if (pattern > 1)
            pn.setBrush(QBrush(pr.color(color), patternStyle(pattern)));
        return pn;
    }
    QBrush brush(int color, int pattern) const
    {
        if (pattern <= 0)
            return Qt::NoBrush;
        if (pattern == 1 || pattern >= 32)
            return QBrush(pr.color(color), pattern == 1 ? Qt::SolidPattern : patternStyle(pattern));
        QBrush b(patternImageImpl(pattern, pr.color(color)));
        // узор Grace задан в пунктах — масштабируем вместе со страницей
        const double k = u / pr.scale();
        b.setTransform(QTransform::fromScale(k, k));
        return b;
    }
    double fontPx(double size) const { return size * FONT_SCALE * u; }

    QString numText(const QString &s) const
    {
        if (!opt.typographicMinus)
            return s;
        QString r = s;
        // дефис-минус перед цифрой → знак минуса (только вне разметки \...)
        for (int i = 0; i < r.size(); ++i) {
            if (r[i] == '-' && i + 1 < r.size() && (r[i + 1].isDigit() || r[i + 1] == '.')
                && (i == 0 || r[i - 1] != '\\')) {
                const bool expo = i > 0 && (r[i - 1] == 'e' || r[i - 1] == 'E');
                if (!expo || true)
                    r[i] = QChar(0x2212);
            }
        }
        return r;
    }

    void extend(const QRectF &r)
    {
        if (r.isValid() || r.width() > 0 || r.height() > 0)
            extent = extent.isNull() ? r : extent.united(r);
    }

    void hit(HitItem h)
    {
        if (info)
            info->items.append(std::move(h));
    }

    QPolygonF text(QPointF vp, const QString &s, int font, double size, int color, double angle,
                   int just)
    {
        if (s.isEmpty())
            return QPolygonF();
        QPolygonF poly = te.draw(p, s, font, fontPx(size), pr.color(color), dev(vp), angle, just);
        extend(poly.boundingRect());
        return poly;
    }
    QRectF textBoxView(QPointF vp, const QString &s, int font, double size, double angle, int just)
    {
        QPolygonF poly = te.outline(s, font, fontPx(size), dev(vp), angle, just);
        QRectF r = poly.boundingRect();
        QPointF a = view(r.topLeft()), b = view(r.bottomRight());
        return QRectF(QPointF(qMin(a.x(), b.x()), qMin(a.y(), b.y())),
                      QPointF(qMax(a.x(), b.x()), qMax(a.y(), b.y())));
    }

    void line(QPointF a, QPointF b)  // в координатах устройства
    {
        if (!finite2(a.x(), a.y()) || !finite2(b.x(), b.y()))
            return;
        if (!clipSeg(a, b, big))
            return;
        p->drawLine(a, b);
    }
    void vline(QPointF va, QPointF vb) { line(dev(va), dev(vb)); }

    // ---- символы
    void symbol(QPointF d, double r, int type, int charCode = 65, int charFont = 0,
                const QColor &charColor = Qt::black)
    {
        if (!finite2(d.x(), d.y()) || !big.contains(d))
            return;
        switch (type) {
        case 1:
            p->drawEllipse(d, r, r);
            break;
        case 2: {
            double s = r * 0.85;
            p->drawRect(QRectF(d.x() - s, d.y() - s, 2 * s, 2 * s));
            break;
        }
        case 3: {
            QPointF pts[4] = {{d.x(), d.y() - r}, {d.x() - r, d.y()}, {d.x(), d.y() + r}, {d.x() + r, d.y()}};
            p->drawPolygon(pts, 4);
            break;
        }
        case 4: case 5: case 6: case 7: {
            const double a = 2.0 / std::sqrt(3.0) * r;
            const double b = r / std::sqrt(3.0);
            QPointF pts[3];
            if (type == 4) {  // вверх
                pts[0] = {d.x(), d.y() - a};
                pts[1] = {d.x() - r, d.y() + b};
                pts[2] = {d.x() + r, d.y() + b};
            } else if (type == 5) {  // влево
                pts[0] = {d.x() - a, d.y()};
                pts[1] = {d.x() + b, d.y() - r};
                pts[2] = {d.x() + b, d.y() + r};
            } else if (type == 6) {  // вниз
                pts[0] = {d.x(), d.y() + a};
                pts[1] = {d.x() - r, d.y() - b};
                pts[2] = {d.x() + r, d.y() - b};
            } else {  // вправо
                pts[0] = {d.x() + a, d.y()};
                pts[1] = {d.x() - b, d.y() - r};
                pts[2] = {d.x() - b, d.y() + r};
            }
            p->drawPolygon(pts, 3);
            break;
        }
        case 8:
            p->drawLine(QPointF(d.x() - r, d.y()), QPointF(d.x() + r, d.y()));
            p->drawLine(QPointF(d.x(), d.y() - r), QPointF(d.x(), d.y() + r));
            break;
        case 9: {
            double s = r * M_SQRT1_2;
            p->drawLine(QPointF(d.x() - s, d.y() - s), QPointF(d.x() + s, d.y() + s));
            p->drawLine(QPointF(d.x() - s, d.y() + s), QPointF(d.x() + s, d.y() - s));
            break;
        }
        case 10: {
            double s = r * M_SQRT1_2;
            p->drawLine(QPointF(d.x() - r, d.y()), QPointF(d.x() + r, d.y()));
            p->drawLine(QPointF(d.x(), d.y() - r), QPointF(d.x(), d.y() + r));
            p->drawLine(QPointF(d.x() - s, d.y() - s), QPointF(d.x() + s, d.y() + s));
            p->drawLine(QPointF(d.x() - s, d.y() + s), QPointF(d.x() + s, d.y() - s));
            break;
        }
        case 11: {
            QString ch = QString(QChar(charCode > 0 ? charCode : 65));
            QPen save = p->pen();
            te.draw(p, ch, charFont, r * 2 / 0.02 * FONT_SCALE, charColor, d, 0, JustCenter | JustMiddle);
            p->setPen(save);
            break;
        }
        default:
            break;
        }
    }

    // ---- главное
    void render()
    {
        p->save();
        p->setRenderHint(QPainter::Antialiasing, true);
        p->setRenderHint(QPainter::TextAntialiasing, true);
        p->setRenderHint(QPainter::SmoothPixmapTransform, true);
        if (opt.drawBackground && pr.bgFill)
            p->fillRect(page, pr.color(pr.bgColor));
        for (const Graph &g : pr.graphs) {
            if (!g.visible())
                continue;
            curGid = g.id;
            extent = QRectF();
            p->save();
            drawGraph(g);
            p->restore();
            if (info)
                info->extents[g.id] = extent;
        }
        curGid = -1;
        for (int i = 0; i < pr.objects.size(); ++i) {
            const DrawObject &o = pr.objects[i];
            if (o.p.s("loctype") == "world")
                continue;  // уже нарисованы вместе с графиком
            drawObject(o, i, nullptr);
        }
        p->restore();
    }

    void drawGraph(const Graph &g)
    {
        WorldMap wm(g);
        if (info)
            info->maps[g.id] = wm;
        const QRectF frameDev(dev(wm.vx1, wm.vy2), dev(wm.vx2, wm.vy1));
        if (info)
            info->frames[g.id] = frameDev;
        extend(frameDev);
        const QString type = g.type();

        // фон рамки
        if (g.p.i("frame_bg_pattern") != 0) {
            if (type == "Polar" || type == "Pie") {
            } else {
                p->fillRect(frameDev, brush(g.p.i("frame_bg_color"), g.p.i("frame_bg_pattern")));
            }
        }

        if (type == "Pie") {
            drawPie(g, wm);
            drawTitles(g, wm);
            drawGraphObjects(g);
            HitItem h;
            h.kind = HitItem::GraphFrame;
            h.gid = g.id;
            h.poly = QPolygonF(frameDev);
            hit(h);
            return;
        }
        if (type == "Polar") {
            drawPolar(g, wm);
            drawGraphObjects(g);
            drawLegend(g, wm);
            drawTitles(g, wm);
            HitItem h;
            h.kind = HitItem::GraphFrame;
            h.gid = g.id;
            h.poly = QPolygonF(frameDev);
            if (info)
                info->items.prepend(h);
            return;
        }

        QVector<Tick> ticks[4];
        for (int a = 0; a < 4; ++a)
            if (g.axes[a].p.b("on"))
                ticks[a] = computeTicks(g, a);

        drawGrid(g, wm, ticks);

        // рамка — самая нижняя цель для мыши
        HitItem fh;
        fh.kind = HitItem::GraphFrame;
        fh.gid = g.id;
        fh.poly = QPolygonF(frameDev);
        hit(fh);

        p->save();
        p->setClipRect(frameDev.adjusted(-0.5, -0.5, 0.5, 0.5));
        drawSets(g, wm);
        p->restore();

        for (int a = 0; a < 4; ++a)
            if (g.axes[a].p.b("on"))
                drawAxis(g, wm, a, ticks[a]);
        drawFrame(g, frameDev);
        drawGraphObjects(g);
        drawLegend(g, wm);
        drawTitles(g, wm);
    }

    void drawGraphObjects(const Graph &g)
    {
        for (int i = 0; i < pr.objects.size(); ++i) {
            const DrawObject &o = pr.objects[i];
            if (o.p.s("loctype") == "world" && o.graph == g.id) {
                WorldMap wm(g);
                drawObject(o, i, &wm);
            }
        }
    }

    // ---- сетка
    void drawGrid(const Graph &g, const WorldMap &wm, const QVector<Tick> *ticks)
    {
        for (int a = 0; a < 4; ++a) {
            const Axis &ax = g.axes[a];
            if (!ax.p.b("on"))
                continue;
            const bool isX = (a % 2) == 0;
            for (int loop = 0; loop < 2; ++loop) {
                const bool major = loop == 1;
                const QString pre = major ? "tick_major_" : "tick_minor_";
                if (!ax.p.b(pre + "grid"))
                    continue;
                p->setPen(pen(ax.p.i(pre + "color"), 1, ax.p.d(pre + "linewidth"), ax.p.i(pre + "linestyle")));
                const double lo = isX ? qMin(wm.wx1, wm.wx2) : qMin(wm.wy1, wm.wy2);
                const double hi = isX ? qMax(wm.wx1, wm.wx2) : qMax(wm.wy1, wm.wy2);
                for (const Tick &t : ticks[a]) {
                    if (t.major != major || t.w < lo - 1e-12 * std::fabs(hi - lo) || t.w > hi + 1e-12 * std::fabs(hi - lo))
                        continue;
                    if (isX) {
                        double v = wm.xv(t.w);
                        vline({v, wm.vy1}, {v, wm.vy2});
                    } else {
                        double v = wm.yv(t.w);
                        vline({wm.vx1, v}, {wm.vx2, v});
                    }
                }
            }
        }
    }

    // ---- рамка
    void drawFrame(const Graph &g, const QRectF &r)
    {
        QPen pn = pen(g.p.i("frame_color"), g.p.i("frame_pattern"), g.p.d("frame_linewidth"),
                      g.p.i("frame_linestyle"));
        if (pn.style() == Qt::NoPen)
            return;
        pn.setCapStyle(Qt::SquareCap);
        pn.setJoinStyle(Qt::MiterJoin);
        p->setPen(pn);
        p->setBrush(Qt::NoBrush);
        const QPointF bl = r.bottomLeft(), br = r.bottomRight(), tl = r.topLeft(), tr = r.topRight();
        switch (g.p.i("frame_type")) {
        case 1: p->drawPolyline(QPolygonF({tl, bl, br})); break;              // полуоткрытая
        case 2: p->drawPolyline(QPolygonF({tl, bl, br, tr})); break;          // без верха
        case 3: p->drawPolyline(QPolygonF({bl, tl, tr, br})); break;          // без низа
        case 4: p->drawPolyline(QPolygonF({bl, br, tr, tl})); break;          // без левой
        case 5: p->drawPolyline(QPolygonF({br, bl, tl, tr})); break;          // без правой
        default: p->drawRect(r); break;
        }
    }

    // ---- оси
    void drawAxis(const Graph &g, const WorldMap &wm, int ai, const QVector<Tick> &ticks)
    {
        const Axis &ax = g.axes[ai];
        const Props &a = ax.p;
        const bool isX = (ai % 2) == 0;
        auto P = [isX](double para, double perp) { return isX ? QPointF(para, perp) : QPointF(perp, para); };
        auto conv = [&](double w) { return isX ? wm.xv(w) : wm.yv(w); };

        const bool zero = a.b("zero");
        const int sign = zero ? -1 : 1;
        double base1, base2;
        const double paraStart = isX ? wm.vx1 : wm.vy1, paraStop = isX ? wm.vx2 : wm.vy2;
        if (zero) {
            const double lo = isX ? qMin(wm.wy1, wm.wy2) : qMin(wm.wx1, wm.wx2);
            const double hi = isX ? qMax(wm.wy1, wm.wy2) : qMax(wm.wx1, wm.wx2);
            if (!(lo <= 0 && hi >= 0))
                return;
            double z = isX ? wm.yv(0.0) : wm.xv(0.0);
            if (!std::isfinite(z))
                return;
            base1 = base2 = z;
        } else {
            base1 = isX ? wm.vy1 : wm.vx1;
            base2 = isX ? wm.vy2 : wm.vx2;
            if (isX ? wm.yinv : wm.xinv)
                std::swap(base1, base2);
        }
        Vec2 off = a.v2("offset");
        base1 -= off[0];
        base2 += off[1];

        const QString tplace = a.s("tick_place");
        const bool onN = tplace == "normal" || tplace == "both";
        const bool onO = tplace == "opposite" || tplace == "both";

        // линия оси
        if (a.b("bar")) {
            p->setPen(pen(a.i("bar_color"), 1, a.d("bar_linewidth"), a.i("bar_linestyle")));
            if (onN)
                vline(P(paraStart, base1), P(paraStop, base1));
            if (onO)
                vline(P(paraStart, base2), P(paraStop, base2));
        }

        // рамка для размещения подписи оси: вьюпорт + засечки + подписи
        double bbMin = isX ? wm.vy1 : wm.vx1, bbMax = isX ? wm.vy2 : wm.vx2;
        auto upd = [&](double perp) {
            bbMin = qMin(bbMin, perp);
            bbMax = qMax(bbMax, perp);
        };
        const double wlo = isX ? qMin(wm.wx1, wm.wx2) : qMin(wm.wy1, wm.wy2);
        const double whi = isX ? qMax(wm.wx1, wm.wx2) : qMax(wm.wy1, wm.wy2);
        const double eps = 1e-9 * std::fabs(whi - wlo);
        const QString inout = a.s("tick_inout");

        if (a.b("tick")) {
            for (int loop = 0; loop < 2; ++loop) {
                const bool major = loop == 1;
                const QString pre = major ? "tick_major_" : "tick_minor_";
                const double tsize = 0.02 * a.d(pre + "size");
                double s1a, s1b, s2a, s2b;
                if (inout == "in") {
                    s1a = base1; s1b = base1 + sign * tsize;
                    s2a = base2; s2b = base2 - sign * tsize;
                } else if (inout == "out") {
                    s1a = base1; s1b = base1 - sign * tsize;
                    s2a = base2; s2b = base2 + sign * tsize;
                } else {
                    s1a = base1 - tsize; s1b = base1 + tsize;
                    s2a = base2 + tsize; s2b = base2 - tsize;
                }
                QPen tp = pen(a.i(pre + "color"), 1, a.d(pre + "linewidth"), a.i(pre + "linestyle"));
                tp.setCapStyle(Qt::FlatCap);
                p->setPen(tp);
                for (const Tick &t : ticks) {
                    if (t.major != major || t.w < wlo - eps || t.w > whi + eps)
                        continue;
                    const double v = conv(t.w);
                    if (!std::isfinite(v))
                        continue;
                    if (onN) {
                        vline(P(v, s1a), P(v, s1b));
                        upd(s1a);
                        upd(s1b);
                    }
                    if (onO) {
                        vline(P(v, s2a), P(v, s2b));
                        upd(s2a);
                        upd(s2b);
                    }
                }
            }
        }

        const bool autoOff = a.s("ticklabel_offset_type") == "auto";
        Vec2 tlo = a.v2("ticklabel_offset");
        const double tlTrans = autoOff ? 0.0 : tlo[0];
        const double tlOffset = autoOff ? 0.01 : tlo[1];
        const int just1 = isX ? (JustCenter | JustTop) : (JustRight | JustMiddle);
        const int just2 = isX ? (JustCenter | JustBottom) : (JustLeft | JustMiddle);

        if (a.b("ticklabel")) {
            const double tsize = 0.02 * a.d("tick_major_size");
            const double tlsize = 0.02 * a.d("ticklabel_char_size");
            double lb1, lb2;
            if (inout == "in") {
                lb1 = base1 - (1 - sign) / 2.0 * tsize - tlOffset;
                lb2 = base2 + (1 - sign) / 2.0 * tsize + tlOffset;
            } else if (inout == "out") {
                lb1 = base1 - (1 + sign) / 2.0 * tsize - tlOffset;
                lb2 = base2 + (1 + sign) / 2.0 * tsize + tlOffset;
            } else {
                lb1 = base1 - tsize - tlOffset;
                lb2 = base2 + tsize + tlOffset;
            }
            if (!a.b("tick")) {  // без засечек подписи ближе
                lb1 = base1 - tlOffset;
                lb2 = base2 + tlOffset;
            }
            double startL = wlo, stopL = whi;
            if (a.s("ticklabel_start_type") == "spec")
                startL = a.d("ticklabel_start");
            if (a.s("ticklabel_stop_type") == "spec")
                stopL = a.d("ticklabel_stop");
            const QString tlplace = a.s("ticklabel_place");
            const bool lN = tlplace == "normal" || tlplace == "both";
            const bool lO = tlplace == "opposite" || tlplace == "both";
            const int skip = qMax(0, a.i("ticklabel_skip"));
            const int stagger = qMax(0, a.i("ticklabel_stagger"));
            const int font = a.i("ticklabel_font");
            const double size = a.d("ticklabel_char_size");
            const int color = a.i("ticklabel_color");
            const double angle = a.i("ticklabel_angle");
            const QString prep = a.s("ticklabel_prepend"), app = a.s("ticklabel_append");
            QRectF labelsDev;
            int itcur = 0;
            for (const Tick &t : ticks) {
                if (!t.major)
                    continue;
                if (t.w < startL - eps || t.w > stopL + eps || t.w < wlo - eps || t.w > whi + eps)
                    continue;
                const double v = conv(t.w);
                if (!std::isfinite(v))
                    continue;
                if (itcur % (skip + 1) == 0) {
                    const QString lab = prep + numText(t.label) + app;
                    const double st = (tlOffset + tlsize) * (itcur % (stagger + 1));
                    if (lN) {
                        QPolygonF poly = text(P(v + tlTrans, lb1 - st), lab, font, size, color, angle, just1);
                        QRectF r = poly.boundingRect();
                        labelsDev |= r;
                        QPointF va = view(r.topLeft()), vb = view(r.bottomRight());
                        upd(isX ? va.y() : va.x());
                        upd(isX ? vb.y() : vb.x());
                    }
                    if (lO) {
                        QPolygonF poly = text(P(v + tlTrans, lb2 + st), lab, font, size, color, angle, just2);
                        QRectF r = poly.boundingRect();
                        labelsDev |= r;
                        QPointF va = view(r.topLeft()), vb = view(r.bottomRight());
                        upd(isX ? va.y() : va.x());
                        upd(isX ? vb.y() : vb.x());
                    }
                }
                ++itcur;
            }
            if (!labelsDev.isNull()) {
                HitItem h;
                h.kind = HitItem::TickLabels;
                h.gid = g.id;
                h.axis = ai;
                h.poly = QPolygonF(labelsDev);
                hit(h);
            }
        }

        // подпись оси
        const QString label = a.s("label");
        if (!label.isEmpty()) {
            double o1x, o1y, o2x, o2y;
            int lj1, lj2;
            if (a.s("label_place_type") == "spec") {
                Vec2 lp = a.v2("label_place_xy");
                o1x = o2x = lp[0];
                o1y = o2y = lp[1];
                lj1 = lj2 = JustCenter | JustMiddle;
            } else {
                o1x = o2x = 0;
                o1y = base1 - bbMin + tlOffset;
                o2y = bbMax - base2 + tlOffset;
                lj1 = just1;
                lj2 = just2;
            }
            const bool perp = a.s("label_layout") == "perp";
            const double langle = isX ? (perp ? 90 : 0) : (perp ? 0 : 90);
            const QString side = a.s("label_side");
            const double mid = (paraStart + paraStop) / 2;
            QPolygonF all;
            if (side == "normal" || side == "both")
                all += text(P(mid + o1x, base1 - o1y), label, a.i("label_font"), a.d("label_char_size"),
                            a.i("label_color"), langle, lj1);
            if (side == "opposite" || side == "both")
                all += text(P(mid + o2x, base2 + o2y), label, a.i("label_font"), a.d("label_char_size"),
                            a.i("label_color"), langle, lj2);
            if (!all.isEmpty()) {
                HitItem h;
                h.kind = HitItem::AxisLabel;
                h.gid = g.id;
                h.axis = ai;
                h.poly = QPolygonF(all.boundingRect());
                hit(h);
            }
        }
    }

    // ---- наборы
    struct ChartCtx {
        bool chart = false, stacked = false;
        QVector<double> refx;
        QVector<double> refy;  // накопленная сумма (для stacked)
    };

    double setYBase(const Graph &g, const DataSet &s, const WorldMap &wm) const
    {
        const auto &y = s.col(1);
        double r = 0;
        switch (s.p.i("baseline_type")) {
        case 1: case 2: case 5: {
            double mn = 1e300, mx = -1e300, sum = 0;
            int n = 0;
            for (double v : y)
                if (std::isfinite(v)) {
                    mn = qMin(mn, v);
                    mx = qMax(mx, v);
                    sum += v;
                    ++n;
                }
            if (n == 0)
                return 0;
            r = s.p.i("baseline_type") == 1 ? mn : s.p.i("baseline_type") == 2 ? mx : sum / n;
            break;
        }
        case 3: r = qMin(wm.wy1, wm.wy2); break;
        case 4: r = qMax(wm.wy1, wm.wy2); break;
        default: r = 0;
        }
        if (!wm.validY(r))
            r = qMin(wm.wy1, wm.wy2);
        Q_UNUSED(g);
        return r;
    }

    static bool isBarType(const QString &t) { return t == "bar" || t == "bardy" || t == "bardydy"; }

    void drawSets(const Graph &g, const WorldMap &wm)
    {
        ChartCtx cc;
        cc.chart = g.type() == "Chart";
        cc.stacked = cc.chart && g.hp.b("stacked");
        QVector<double> offsets(g.sets.size(), 0.0);
        if (cc.chart) {
            for (const auto &s : g.sets)
                if (!s.hidden() && s.size() > 0) {
                    cc.refx = s.col(0);
                    break;
                }
            if (!cc.stacked) {
                // бары рядом: смещение каждого набора
                double total = 0;
                int nb = 0;
                const double gap = g.hp.d("bar_hgap");
                for (const auto &s : g.sets)
                    if (!s.hidden() && s.size() > 0 && isBarType(s.type())) {
                        total += 0.02 * s.p.d("symbol_size");
                        ++nb;
                    }
                total += gap * qMax(0, nb - 1);
                double cur = -total / 2;
                for (int i = 0; i < g.sets.size(); ++i) {
                    const auto &s = g.sets[i];
                    if (s.hidden() || s.size() == 0)
                        continue;
                    if (isBarType(s.type())) {
                        const double w = 0.02 * s.p.d("symbol_size");
                        offsets[i] = cur + w / 2;
                        cur += w + gap;
                    }
                }
            }
            cc.refy = QVector<double>(cc.refx.size(), 0.0);
        }
        for (int i = 0; i < g.sets.size(); ++i) {
            const DataSet &s = g.sets[i];
            if (s.hidden() || s.size() == 0)
                continue;
            drawSet(g, s, wm, cc, offsets[i]);
            if (cc.stacked) {
                const auto &y = s.col(1);
                for (int k = 0; k < cc.refy.size() && k < y.size(); ++k)
                    if (std::isfinite(y[k]))
                        cc.refy[k] += y[k];
            }
        }
    }

    // точки набора во вьюпорте (с учётом диаграмм), NaN — разрыв
    QVector<QPointF> setPointsView(const DataSet &s, const WorldMap &wm, const ChartCtx &cc, double offset,
                                   int &n) const
    {
        const auto &x = cc.chart ? cc.refx : s.col(0);
        const auto &y = s.col(1);
        n = qMin(x.size(), y.size());
        QVector<QPointF> v(n);
        const double nan = std::numeric_limits<double>::quiet_NaN();
        for (int i = 0; i < n; ++i) {
            double yy = y[i];
            if (cc.stacked && i < cc.refy.size())
                yy += cc.refy[i];
            if (!wm.validX(x[i]) || !wm.validY(yy) || !std::isfinite(x[i]) || !std::isfinite(yy)) {
                v[i] = QPointF(nan, nan);
                continue;
            }
            v[i] = QPointF(wm.xv(x[i]) + offset, wm.yv(yy));
        }
        return v;
    }

    void drawSet(const Graph &g, const DataSet &s, const WorldMap &wm, const ChartCtx &cc, double offset)
    {
        const QString t = s.type();
        int n = 0;
        QVector<QPointF> pv = setPointsView(s, wm, cc, offset, n);
        QVector<QPointF> pd(n);
        for (int i = 0; i < n; ++i)
            pd[i] = std::isfinite(pv[i].x()) ? dev(pv[i]) : pv[i];

        const double ybase = cc.stacked ? 0.0 : setYBase(g, s, wm);
        if (t != "xyhilo")  // как в Grace: у hilo нет соединительной линии
            drawSetLine(g, s, wm, cc, pv, pd, ybase, offset);
        if (isBarType(t))
            drawBars(s, wm, cc, pv, ybase, offset);
        if (t == "xyhilo")
            drawHiLo(s, wm);
        else if (t == "xyboxplot")
            drawBoxplot(s, wm);
        else if (t == "xyvmap")
            drawVmap(g, s, pd);
        if (t.startsWith("xyd") || t == "bardy" || t == "bardydy")
            drawErrorBars(s, wm, pv, offset);
        if (!isBarType(t) && t != "xyhilo" && t != "xyboxplot")
            drawSymbols(g, s, pd, wm);
        if (t == "xyr")
            drawCircles(s, wm, pd);
        drawAValues(s, pv, cc);

        // для выделения мышью
        HitItem h;
        h.kind = HitItem::Set;
        h.gid = g.id;
        h.sid = s.id;
        const int step = qMax(1, n / 4000);
        for (int i = 0; i < n; i += step)
            if (std::isfinite(pd[i].x()))
                h.poly << clampPt(pd[i]);
        h.polyline = s.p.i("line_type") != 0 && s.p.i("line_linestyle") != 0 && !isBarType(t);
        h.points = !h.polyline || s.p.i("symbol") != 0 || isBarType(t);
        hit(h);
    }

    void drawSetLine(const Graph &g, const DataSet &s, const WorldMap &wm, const ChartCtx &cc,
                     const QVector<QPointF> &pv, const QVector<QPointF> &pd, double ybase, double offset)
    {
        Q_UNUSED(g);
        const int n = pd.size();
        const int ltype = s.p.i("line_type");
        const double ybv = wm.yv(ybase);

        // заливка (как в Grace — только если есть тип линии)
        const int ftype = s.p.i("fill_type");
        if (ftype != 0 && ltype != 0 && s.p.i("fill_pattern") != 0 && n > 1) {
            QPolygonF poly;
            QPointF prev;
            bool have = false;
            for (int i = 0; i < n; ++i) {
                if (!std::isfinite(pd[i].x()))
                    continue;
                QPointF q = clampPt(pd[i]);
                if (have && (ltype == 2 || ltype == 3))
                    poly << (ltype == 2 ? QPointF(prev.x(), q.y()) : QPointF(q.x(), prev.y()));
                poly << q;
                prev = q;
                have = true;
            }
            if (poly.size() > 1) {
                if (ftype == 2) {
                    if (cc.stacked) {
                        for (int i = n - 1; i >= 0; --i) {
                            if (!std::isfinite(pv[i].x()) || i >= cc.refx.size())
                                continue;
                            double yb = i < cc.refy.size() ? cc.refy[i] : 0;
                            poly << clampPt(dev(wm.xv(cc.refx[i]) + offset, wm.yv(yb)));
                        }
                    } else {
                        poly << clampPt(dev(view(poly.last()).x(), ybv));
                        poly << clampPt(dev(view(poly.first()).x(), ybv));
                    }
                }
                p->setPen(Qt::NoPen);
                p->setBrush(brush(s.p.i("fill_color"), s.p.i("fill_pattern")));
                p->drawPolygon(poly, s.p.i("fill_rule") == 1 ? Qt::OddEvenFill : Qt::WindingFill);
                p->setBrush(Qt::NoBrush);
            }
        }

        const QPen lp = pen(s.p.i("line_color"), s.p.i("line_pattern"), s.p.d("line_linewidth"),
                            s.p.i("line_linestyle"), true);
        if (lp.style() != Qt::NoPen && ltype != 0) {
            p->setPen(lp);
            p->setBrush(Qt::NoBrush);
            // собрать непрерывные куски
            QVector<QVector<QPointF>> runs;
            QVector<QPointF> cur;
            for (int i = 0; i < n; ++i) {
                if (!std::isfinite(pd[i].x())) {
                    if (!cur.isEmpty())
                        runs << cur;
                    cur.clear();
                    continue;
                }
                cur << pd[i];
            }
            if (!cur.isEmpty())
                runs << cur;
            for (auto &r : runs) {
                if (ltype == 1) {
                    drawPolyline(decimate(r));
                } else if (ltype == 2 || ltype == 3) {
                    QVector<QPointF> st;
                    st.reserve(r.size() * 2);
                    for (int i = 0; i < r.size(); ++i) {
                        if (i > 0) {
                            if (ltype == 2)
                                st << QPointF(r[i - 1].x(), r[i].y());
                            else
                                st << QPointF(r[i].x(), r[i - 1].y());
                        }
                        st << r[i];
                    }
                    drawPolyline(st);
                } else if (ltype == 4) {
                    for (int i = 0; i + 1 < r.size(); i += 2)
                        line(r[i], r[i + 1]);
                } else if (ltype == 5) {
                    int i = 0;
                    for (; i + 2 < r.size(); i += 3)
                        drawPolyline({r[i], r[i + 1], r[i + 2]});
                    if (i + 1 < r.size())
                        line(r[i], r[i + 1]);
                }
            }
        }

        // выносные линии и базовая линия
        if (s.p.b("dropline") && lp.style() != Qt::NoPen) {
            p->setPen(lp);
            for (int i = 0; i < n; ++i) {
                if (!std::isfinite(pv[i].x()))
                    continue;
                double yb = ybv;
                if (cc.stacked && i < cc.refy.size())
                    yb = wm.yv(cc.refy[i]);
                vline({pv[i].x(), yb}, pv[i]);
            }
        }
        if (s.p.b("baseline") && !cc.stacked && lp.style() != Qt::NoPen) {
            double mn = 1e300, mx = -1e300;
            for (const auto &q : pv)
                if (std::isfinite(q.x())) {
                    mn = qMin(mn, q.x());
                    mx = qMax(mx, q.x());
                }
            if (mn < mx) {
                p->setPen(lp);
                vline({mn, ybv}, {mx, ybv});
            }
        }
    }

    // Прореживание плотных монотонных кривых: min/max на пиксель
    QVector<QPointF> decimate(const QVector<QPointF> &r) const
    {
        if (r.size() < 20000)
            return r;
        bool mono = true;
        for (int i = 1; i < r.size() && mono; ++i)
            mono = r[i].x() >= r[i - 1].x();
        if (!mono)
            return r;
        QVector<QPointF> out;
        out.reserve(4 * int(std::abs(r.last().x() - r.first().x())) + 8);
        int i = 0;
        while (i < r.size()) {
            const double col = std::floor(r[i].x());
            QPointF first = r[i], last = r[i], mn = r[i], mx = r[i];
            int j = i + 1;
            while (j < r.size() && std::floor(r[j].x()) == col) {
                last = r[j];
                if (r[j].y() < mn.y()) mn = r[j];
                if (r[j].y() > mx.y()) mx = r[j];
                ++j;
            }
            out << first;
            if (j - i > 2) {
                if (mn.x() < mx.x()) out << mn << mx;
                else out << mx << mn;
            }
            if (j - i > 1)
                out << last;
            i = j;
        }
        return out;
    }

    void drawPolyline(const QVector<QPointF> &pts)
    {
        if (pts.size() < 2)
            return;
        for (const QPolygonF &poly : safePolylines(pts, big))
            p->drawPolyline(poly);
    }

    void symbolStyle(const DataSet &s, int colorOverride = -1, int patternOverride = -1)
    {
        const int sc = colorOverride >= 0 ? colorOverride : s.p.i("symbol_color");
        const int fc = colorOverride >= 0 ? colorOverride : s.p.i("symbol_fill_color");
        const int fp = patternOverride >= 0 ? patternOverride : s.p.i("symbol_fill_pattern");
        QPen sp = pen(sc, s.p.i("symbol_pattern"), s.p.d("symbol_linewidth"), s.p.i("symbol_linestyle"), true);
        p->setPen(sp);
        p->setBrush(brush(fc, fp));
    }

    void drawSymbols(const Graph &g, const DataSet &s, const QVector<QPointF> &pd, const WorldMap &wm)
    {
        Q_UNUSED(wm);
        const int sym = s.p.i("symbol");
        if (sym == 0)
            return;
        const QString t = s.type();
        const int skip = qMax(0, s.p.i("symbol_skip")) + 1;
        const double r0 = 0.01 * s.p.d("symbol_size") * u;
        const double znorm = g.p.d("znorm") != 0 ? g.p.d("znorm") : 1.0;
        const auto &z = s.col(2);
        const auto &c4 = s.col(3);
        symbolStyle(s);
        for (int i = 0; i < pd.size(); i += skip) {
            if (!std::isfinite(pd[i].x()))
                continue;
            double r = r0;
            if (t == "xysize") {
                r = 0.01 * (i < z.size() ? z[i] : 1.0) / znorm * u;
                if (!(r > 0))
                    continue;
            }
            if (t == "xycolor" && i < z.size())
                symbolStyle(s, int(z[i]));
            else if (t == "xycolpat" && i < z.size())
                symbolStyle(s, int(z[i]), i < c4.size() ? int(c4[i]) : -1);
            symbol(pd[i], r, sym, s.p.i("symbol_char"), s.p.i("symbol_char_font"),
                   pr.color(s.p.i("symbol_color")));
        }
        p->setBrush(Qt::NoBrush);
    }

    void drawCircles(const DataSet &s, const WorldMap &wm, const QVector<QPointF> &pd)
    {
        const auto &x = s.col(0);
        const auto &r = s.col(2);
        p->setPen(pen(s.p.i("line_color"), s.p.i("line_pattern"), s.p.d("line_linewidth"),
                      s.p.i("line_linestyle"), true));
        p->setBrush(brush(s.p.i("fill_color"), s.p.i("fill_type") ? s.p.i("fill_pattern") : 0));
        for (int i = 0; i < pd.size() && i < r.size(); ++i) {
            if (!std::isfinite(pd[i].x()))
                continue;
            double rx = std::fabs(wm.xv(x[i] + r[i]) - wm.xv(x[i])) * u;
            if (std::isfinite(rx) && rx < 1e6)
                p->drawEllipse(pd[i], rx, rx);
        }
        p->setBrush(Qt::NoBrush);
    }

    void drawBars(const DataSet &s, const WorldMap &wm, const ChartCtx &cc, const QVector<QPointF> &pv,
                  double ybase, double offset)
    {
        Q_UNUSED(offset);
        const double bw = 0.01 * s.p.d("symbol_size");
        const int skip = qMax(0, s.p.i("symbol_skip")) + 1;
        const QBrush fb = brush(s.p.i("symbol_fill_color"), s.p.i("symbol_fill_pattern"));
        const QPen sp = pen(s.p.i("symbol_color"), s.p.i("symbol_pattern"), s.p.d("symbol_linewidth"),
                            s.p.i("symbol_linestyle"));
        const double ybv = wm.yv(ybase);
        for (int i = 0; i < pv.size(); i += skip) {
            if (!std::isfinite(pv[i].x()))
                continue;
            double yb = ybv;
            if (cc.stacked && i < cc.refy.size())
                yb = wm.yv(cc.refy[i]);
            if (!std::isfinite(yb))
                yb = wm.vy1;
            QRectF r(dev(pv[i].x() - bw, qMax(yb, pv[i].y())), dev(pv[i].x() + bw, qMin(yb, pv[i].y())));
            r = r.normalized().intersected(big);
            p->setPen(Qt::NoPen);
            p->setBrush(fb);
            p->drawRect(r);
            p->setBrush(Qt::NoBrush);
            p->setPen(sp);
            p->drawRect(r);
        }
    }

    void errorBar(QPointF va, QPointF vb, bool horizontalRiser, const DataSet &s, const QPen &riser,
                  const QPen &cap)
    {
        if (!finite2(vb.x(), vb.y()) || !finite2(va.x(), va.y()))
            return;
        // обрезка «стойки»
        if (s.p.b("errorbar_riser_clip")) {
            const double L = s.p.d("errorbar_riser_clip_length");
            QPointF d = vb - va;
            const double len = std::hypot(d.x(), d.y());
            if (len > L && len > 0) {
                vb = va + d * (L / len);
                p->setPen(riser);
                vline(va, vb);
                p->setPen(cap);
                arrowHead(dev(va), dev(vb), 2 * s.p.d("errorbar_size"), 0, 1, 1, cap.color());
                return;
            }
        }
        p->setPen(riser);
        vline(va, vb);
        const double il = 0.01 * s.p.d("errorbar_size");
        p->setPen(cap);
        if (horizontalRiser)
            vline({vb.x(), vb.y() - il}, {vb.x(), vb.y() + il});
        else
            vline({vb.x() - il, vb.y()}, {vb.x() + il, vb.y()});
    }

    void drawErrorBars(const DataSet &s, const WorldMap &wm, const QVector<QPointF> &pv, double offset)
    {
        if (!s.p.b("errorbar"))
            return;
        const QString t = s.type();
        const QString place = s.p.s("errorbar_place");
        const bool plus = place == "normal" || place == "both";
        const bool minus = place == "opposite" || place == "both";
        const int ec = s.p.i("errorbar_color"), epat = s.p.i("errorbar_pattern");
        const QPen riser = pen(ec, epat, s.p.d("errorbar_riser_linewidth"), s.p.i("errorbar_riser_linestyle"));
        QPen cap = pen(ec, epat, s.p.d("errorbar_linewidth"), s.p.i("errorbar_linestyle"));
        cap.setCapStyle(Qt::FlatCap);
        const auto &x = s.col(0), &y = s.col(1);
        const auto &c2 = s.col(2), &c3 = s.col(3), &c4 = s.col(4), &c5 = s.col(5);
        auto val = [](const QVector<double> &c, int i) { return i < c.size() ? c[i] : 0.0; };
        for (int i = 0; i < pv.size(); ++i) {
            if (!std::isfinite(pv[i].x()))
                continue;
            const QPointF o = pv[i];
            double dxp = 0, dxm = 0, dyp = 0, dym = 0;
            if (t == "xydx") dxp = dxm = val(c2, i);
            else if (t == "xydy" || t == "bardy") dyp = dym = val(c2, i);
            else if (t == "xydxdx") { dxp = val(c2, i); dxm = val(c3, i); }
            else if (t == "xydydy" || t == "bardydy") { dyp = val(c2, i); dym = val(c3, i); }
            else if (t == "xydxdy") { dxp = dxm = val(c2, i); dyp = dym = val(c3, i); }
            else if (t == "xydxdxdydy") { dxp = val(c2, i); dxm = val(c3, i); dyp = val(c4, i); dym = val(c5, i); }
            const double xi = x[i], yi = y[i];
            if (dxp != 0 && plus)
                errorBar(o, {wm.xv(xi + std::fabs(dxp)) + offset, o.y()}, true, s, riser, cap);
            if (dxm != 0 && minus)
                errorBar(o, {wm.xv(xi - std::fabs(dxm)) + offset, o.y()}, true, s, riser, cap);
            if (dyp != 0 && plus)
                errorBar(o, {o.x(), wm.yv(yi + std::fabs(dyp))}, false, s, riser, cap);
            if (dym != 0 && minus)
                errorBar(o, {o.x(), wm.yv(yi - std::fabs(dym))}, false, s, riser, cap);
        }
    }

    void drawHiLo(const DataSet &s, const WorldMap &wm)
    {
        const double il = 0.02 * s.p.d("symbol_size");
        p->setPen(pen(s.p.i("symbol_color"), s.p.i("symbol_pattern"), s.p.d("symbol_linewidth"),
                      s.p.i("symbol_linestyle")));
        const auto &x = s.col(0), &hi = s.col(1), &lo = s.col(2), &op = s.col(3), &cl = s.col(4);
        const int skip = qMax(0, s.p.i("symbol_skip")) + 1;
        for (int i = 0; i < x.size(); i += skip) {
            const double xv = wm.xv(x[i]);
            if (!std::isfinite(xv))
                continue;
            vline({xv, wm.yv(lo.value(i))}, {xv, wm.yv(hi.value(i))});
            vline({xv - il, wm.yv(op.value(i))}, {xv, wm.yv(op.value(i))});
            vline({xv, wm.yv(cl.value(i))}, {xv + il, wm.yv(cl.value(i))});
        }
    }

    void drawBoxplot(const DataSet &s, const WorldMap &wm)
    {
        const double size = 0.01 * s.p.d("symbol_size");
        const auto &x = s.col(0), &md = s.col(1), &lb = s.col(2), &ub = s.col(3), &lw = s.col(4), &uw = s.col(5);
        const int ec = s.p.i("errorbar_color"), epat = s.p.i("errorbar_pattern");
        const QPen riser = pen(ec, epat, s.p.d("errorbar_riser_linewidth"), s.p.i("errorbar_riser_linestyle"));
        const QPen cap = pen(ec, epat, s.p.d("errorbar_linewidth"), s.p.i("errorbar_linestyle"));
        const QPen sp = pen(s.p.i("symbol_color"), s.p.i("symbol_pattern"), s.p.d("symbol_linewidth"),
                            s.p.i("symbol_linestyle"));
        const QBrush fb = brush(s.p.i("symbol_fill_color"), s.p.i("symbol_fill_pattern"));
        const int skip = qMax(0, s.p.i("symbol_skip")) + 1;
        for (int i = 0; i < x.size(); i += skip) {
            const double xv = wm.xv(x[i]);
            if (!std::isfinite(xv))
                continue;
            QPointF v1(xv, wm.yv(lb.value(i))), v2(xv, wm.yv(ub.value(i)));
            if (s.p.b("errorbar")) {
                errorBar(v1, {xv, wm.yv(lw.value(i))}, false, s, riser, cap);
                errorBar(v2, {xv, wm.yv(uw.value(i))}, false, s, riser, cap);
            }
            QRectF r = QRectF(dev(xv - size, v1.y()), dev(xv + size, v2.y())).normalized();
            p->setPen(Qt::NoPen);
            p->setBrush(fb);
            p->drawRect(r);
            p->setBrush(Qt::NoBrush);
            p->setPen(sp);
            p->drawRect(r);
            const double m = wm.yv(md.value(i));
            vline({xv - size, m}, {xv + size, m});
        }
    }

    void arrowHead(QPointF a, QPointF b, double lengthVp, int type, double dl, double ll, const QColor &c)
    {
        // a, b в координатах устройства; стрелка на конце b
        const double len = std::hypot(b.x() - a.x(), b.y() - a.y());
        if (len == 0)
            return;
        const QPointF nrm((b.x() - a.x()) / len, (b.y() - a.y()) / len);
        const double L = 0.01 * lengthVp * u;
        const double d = L * dl, l = L * ll;
        QPointF c0 = b - nrm * L;
        QPointF vl = c0 + QPointF(-nrm.y(), nrm.x()) * (0.5 * d);
        QPointF vr = c0 - QPointF(-nrm.y(), nrm.x()) * (0.5 * d);
        QPointF vc = c0 + nrm * l;
        QPen pn = p->pen();
        pn.setStyle(Qt::SolidLine);
        pn.setJoinStyle(Qt::MiterJoin);
        p->save();
        p->setPen(pn);
        if (type == 0) {
            p->setBrush(Qt::NoBrush);
            p->drawPolyline(QPolygonF({vl, b, vr}));
        } else {
            p->setBrush(type == 1 ? QBrush(c) : QBrush(pr.color(pr.bgColor)));
            p->drawPolygon(QPolygonF({vl, b, vr, vc}));
        }
        p->restore();
    }

    void drawVmap(const Graph &g, const DataSet &s, const QVector<QPointF> &pd)
    {
        const double znorm = g.p.d("znorm");
        if (znorm == 0)
            return;
        const auto &vx = s.col(2), &vy = s.col(3);
        const int ec = s.p.i("errorbar_color");
        const QPen riser = pen(ec, s.p.i("errorbar_pattern"), s.p.d("errorbar_riser_linewidth"),
                               s.p.i("errorbar_riser_linestyle"));
        const int skip = qMax(0, s.p.i("symbol_skip")) + 1;
        for (int i = 0; i < pd.size(); i += skip) {
            if (!std::isfinite(pd[i].x()))
                continue;
            QPointF b = pd[i] + QPointF(vx.value(i) / znorm * u, -vy.value(i) / znorm * u);
            p->setPen(riser);
            line(pd[i], b);
            arrowHead(pd[i], b, 2 * s.p.d("errorbar_size"), 0, 1, 1, pr.color(ec));
        }
    }

    void drawAValues(const DataSet &s, const QVector<QPointF> &pv, const ChartCtx &cc)
    {
        const bool isString = s.type() == "xystring";
        if (!s.p.b("avalue") && !isString)
            return;
        int type = s.p.i("avalue_type");
        if (isString && !s.p.b("avalue"))
            type = 4;
        if (type == 0)
            return;
        const auto &x = cc.chart ? cc.refx : s.col(0);
        const auto &y = s.col(1);
        const auto &z = s.col(2);
        const QString fmt = s.p.s("avalue_format");
        const int prec = s.p.i("avalue_prec");
        const QString pre = s.p.s("avalue_prepend"), app = s.p.s("avalue_append");
        const Vec2 off = s.p.v2("avalue_offset");
        const int skip = qMax(0, s.p.i("symbol_skip")) + 1;
        for (int i = 0; i < pv.size(); i += skip) {
            if (!std::isfinite(pv[i].x()))
                continue;
            QString str;
            switch (type) {
            case 1: str = numText(formatNumber(x.value(i), fmt, prec)); break;
            case 2: str = numText(formatNumber(y.value(i), fmt, prec)); break;
            case 3:
                str = numText(formatNumber(x.value(i), fmt, prec)) + ", " + numText(formatNumber(y.value(i), fmt, prec));
                break;
            case 4: str = s.strings.value(i); break;
            case 5: str = numText(formatNumber(z.value(i), fmt, prec)); break;
            default: break;
            }
            if (type != 4)
                str = pre + str + app;
            if (str.isEmpty())
                continue;
            text(pv[i] + QPointF(off[0], off[1]), str, s.p.i("avalue_font"), s.p.d("avalue_char_size"),
                 s.p.i("avalue_color"), s.p.i("avalue_rot"), JustCenter | JustBottom);
        }
    }

    // ---- легенда
    void drawLegendSymbol(const DataSet &s, QPointF vpL, QPointF vpR, bool withLine)
    {
        const bool bar = isBarType(s.type()) || s.type() == "xyboxplot";
        auto sym = [&](QPointF v) {
            if (bar) {
                const double w = 0.01 * s.p.d("symbol_size");
                QRectF r(dev(v.x() - w, v.y() + w), dev(v.x() + w, v.y() - w));
                p->setPen(Qt::NoPen);
                p->setBrush(brush(s.p.i("symbol_fill_color"), s.p.i("symbol_fill_pattern")));
                p->drawRect(r);
                p->setBrush(Qt::NoBrush);
                p->setPen(pen(s.p.i("symbol_color"), s.p.i("symbol_pattern"), s.p.d("symbol_linewidth"),
                              s.p.i("symbol_linestyle")));
                p->drawRect(r);
            } else if (s.p.i("symbol") != 0) {
                symbolStyle(s);
                symbol(dev(v), 0.01 * s.p.d("symbol_size") * u, s.p.i("symbol"), s.p.i("symbol_char"),
                       s.p.i("symbol_char_font"), pr.color(s.p.i("symbol_color")));
                p->setBrush(Qt::NoBrush);
            }
        };
        if (withLine) {
            // заливка под линией — полоской
            if (s.p.i("fill_type") != 0 && s.p.i("fill_pattern") != 0) {
                const double h = 0.006;
                p->setPen(Qt::NoPen);
                p->setBrush(brush(s.p.i("fill_color"), s.p.i("fill_pattern")));
                p->drawRect(QRectF(dev(vpL.x(), vpL.y() + h), dev(vpR.x(), vpL.y() - h)));
                p->setBrush(Qt::NoBrush);
            }
            p->setPen(pen(s.p.i("line_color"), s.p.i("line_pattern"), s.p.d("line_linewidth"),
                          s.p.i("line_linestyle"), true));
            vline(vpL, vpR);
            sym(vpL);
            sym(vpR);
        } else {
            sym((vpL + vpR) / 2);
        }
    }

    void drawLegend(const Graph &g, const WorldMap &wm)
    {
        if (!g.p.b("legend"))
            return;
        QVector<const DataSet *> ents;
        double maxsym = 0;
        for (const auto &s : g.sets) {
            if (s.hidden() || s.size() == 0)
                continue;
            if (!s.p.s("legend").isEmpty())
                ents << &s;
            maxsym = qMax(maxsym, s.p.d("symbol_size"));
        }
        if (ents.isEmpty())
            return;
        if (g.p.b("legend_invert"))
            std::reverse(ents.begin(), ents.end());
        Vec2 lp = g.p.v2("legend_pos");
        QPointF vp(lp[0], lp[1]);
        if (g.p.s("legend_loctype") == "world")
            vp = wm.toView(lp[0], lp[1]);
        if (!finite2(vp.x(), vp.y()))
            return;
        const double ldist = 0.01 * g.p.i("legend_length");
        const double hgap = 0.01 * g.p.i("legend_hgap"), vgap = 0.01 * g.p.i("legend_vgap");
        const double sdist = 0.01 * (g.p.i("legend_hgap") + maxsym);
        const double yskip = vgap;
        const int font = g.p.i("legend_font");
        const double csize = g.p.d("legend_char_size");

        // проход 1: раскладка во вьюпорте
        struct Ent { const DataSet *s; QPointF strPos; double midY; };
        QVector<Ent> layout;
        double top = vp.y(), bottom = vp.y(), left = vp.x(), right = vp.x();
        double sy = vp.y();
        const double strX = vp.x() + ldist + sdist;
        for (const DataSet *s : ents) {
            QRectF tb = textBoxView({strX, sy}, s->p.s("legend"), font, csize, 0, JustLeft | JustTop);
            const double bot = tb.isNull() ? sy : tb.top();  // top() — меньшее y во вьюпорте
            const double mid = (sy + bot) / 2;
            layout.append({s, QPointF(strX, sy), mid});
            right = qMax(right, tb.right());
            const bool withLine = g.p.i("legend_length") != 0 && s->p.i("line_linestyle") != 0 && s->p.i("line_type") != 0;
            const double r = 0.01 * s->p.d("symbol_size");
            if (s->p.i("symbol") != 0 || isBarType(s->type())) {
                left = qMin(left, (withLine ? vp.x() : vp.x() + ldist / 2) - r);
                top = qMax(top, mid + r);
                bottom = qMin(bottom, mid - r);
            }
            bottom = qMin(bottom, bot);
            sy = bot - yskip;
        }
        // рамка: содержимое + поля hgap/vgap; содержимое сдвигается как в Grace
        const double w = right - left, h = top - bottom;
        const QPointF shift(vp.x() - left + hgap, vp.y() - top - vgap);
        const QRectF boxV(QPointF(vp.x(), vp.y() - h - 2 * vgap), QSizeF(w + 2 * hgap, h + 2 * vgap));
        const QRectF boxD = QRectF(dev(boxV.left(), boxV.bottom()), dev(boxV.right(), boxV.top())).normalized();

        if (g.p.i("legend_box_fill_pattern") != 0) {
            p->setPen(Qt::NoPen);
            p->setBrush(brush(g.p.i("legend_box_fill_color"), g.p.i("legend_box_fill_pattern")));
            p->drawRect(boxD);
            p->setBrush(Qt::NoBrush);
        }
        QPen bp = pen(g.p.i("legend_box_color"), g.p.i("legend_box_pattern"), g.p.d("legend_box_linewidth"),
                      g.p.i("legend_box_linestyle"));
        if (bp.style() != Qt::NoPen) {
            bp.setJoinStyle(Qt::MiterJoin);
            p->setPen(bp);
            p->drawRect(boxD);
        }
        extend(boxD);

        for (const Ent &e : layout) {
            const DataSet &s = *e.s;
            const QPointF sp = e.strPos + shift;
            const double my = e.midY + shift.y();
            const double x0 = vp.x() + shift.x();
            const bool withLine = g.p.i("legend_length") != 0 && s.p.i("line_linestyle") != 0 && s.p.i("line_type") != 0
                                  && s.p.i("line_pattern") != 0;
            drawLegendSymbol(s, {x0, my}, {x0 + ldist, my}, withLine);
            text(sp, s.p.s("legend"), font, csize, g.p.i("legend_color"), 0, JustLeft | JustTop);
        }
        HitItem hi;
        hi.kind = HitItem::Legend;
        hi.gid = g.id;
        hi.poly = QPolygonF(boxD);
        hit(hi);
    }

    void drawTitles(const Graph &g, const WorldMap &wm)
    {
        const double xc = (wm.vx1 + wm.vx2) / 2;
        const double top = wm.vy2;
        if (!g.p.s("title").isEmpty()) {
            HitItem h;
            h.kind = HitItem::Title;
            h.gid = g.id;
            h.poly = text({xc, top + 0.06}, g.p.s("title"), g.p.i("title_font"), g.p.d("title_size"),
                          g.p.i("title_color"), 0, JustCenter | JustBottom);
            hit(h);
        }
        if (!g.p.s("subtitle").isEmpty()) {
            HitItem h;
            h.kind = HitItem::Subtitle;
            h.gid = g.id;
            h.poly = text({xc, top + 0.02}, g.p.s("subtitle"), g.p.i("subtitle_font"), g.p.d("subtitle_size"),
                          g.p.i("subtitle_color"), 0, JustCenter | JustBottom);
            hit(h);
        }
    }

    // ---- объекты
    void drawObject(const DrawObject &o, int index, const WorldMap *wm)
    {
        if (!o.p.b("on"))
            return;
        auto V = [&](double x, double y) { return wm ? wm->toView(x, y) : QPointF(x, y); };
        HitItem h;
        h.kind = HitItem::Object;
        h.obj = index;
        h.gid = wm ? o.graph : -1;
        if (o.kind == "string") {
            Vec2 pos = o.p.v2("pos");
            h.poly = text(V(pos[0], pos[1]), o.p.s("text"), o.p.i("font"), o.p.d("char_size"), o.p.i("color"),
                          o.p.i("rot"), o.p.i("just"));
            if (h.poly.isEmpty()) {
                QPointF d = dev(V(pos[0], pos[1]));
                h.poly = QPolygonF(QRectF(d.x() - 3, d.y() - 3, 6, 6));
            }
        } else if (o.kind == "line") {
            Vec4 c = o.p.v4("coords");
            QPointF a = dev(V(c[0], c[1])), b = dev(V(c[2], c[3]));
            if (!finite2(a.x(), a.y()) || !finite2(b.x(), b.y()))
                return;
            QPen pn = pen(o.p.i("color"), 1, o.p.d("linewidth"), o.p.i("linestyle"));
            p->setPen(pn);
            line(a, b);
            const int arrow = o.p.i("arrow");
            Vec2 lay = o.p.v2("arrow_layout");
            if (pn.style() != Qt::NoPen) {
                if (arrow == 1 || arrow == 3)
                    arrowHead(b, a, o.p.d("arrow_length"), o.p.i("arrow_type"), lay[0], lay[1], pr.color(o.p.i("color")));
                if (arrow == 2 || arrow == 3)
                    arrowHead(a, b, o.p.d("arrow_length"), o.p.i("arrow_type"), lay[0], lay[1], pr.color(o.p.i("color")));
            }
            h.poly = QPolygonF({a, b});
            h.polyline = true;
            extend(QRectF(a, b).normalized());
        } else {
            Vec4 c = o.p.v4("coords");
            QRectF r = QRectF(dev(V(c[0], c[1])), dev(V(c[2], c[3]))).normalized();
            if (!finite2(r.left(), r.top()) || !finite2(r.right(), r.bottom()))
                return;
            p->setPen(pen(o.p.i("color"), 1, o.p.d("linewidth"), o.p.i("linestyle")));
            p->setBrush(brush(o.p.i("fill_color"), o.p.i("fill_pattern")));
            if (o.kind == "ellipse")
                p->drawEllipse(r);
            else
                p->drawRect(r);
            p->setBrush(Qt::NoBrush);
            h.poly = QPolygonF(r);
            extend(r);
        }
        hit(h);
    }

    // ---- круговая диаграмма (как в Grace: x — доля, y — вынос, y1 — цвет, y2 — узор)
    void drawPie(const Graph &g, const WorldMap &wm)
    {
        const QPointF c((wm.vx1 + wm.vx2) / 2, (wm.vy1 + wm.vy2) / 2);
        const int sgn = g.p.b("xinvert") ? -1 : 1;
        for (const DataSet &s : g.sets) {
            if (s.hidden() || s.size() == 0)
                continue;
            const QString t = s.type();
            const auto &x = s.col(0), &e = s.col(1), &cc = s.col(2), &pt = s.col(3);
            double emax = 0, norm = 0;
            for (int i = 0; i < x.size(); ++i) {
                emax = qMax(emax, e.value(i));
                if (x[i] > 0)
                    norm += x[i];
            }
            if (norm <= 0)
                continue;
            const double r = 0.8 / (1.0 + emax) * qMin(wm.vx2 - wm.vx1, wm.vy2 - wm.vy1) / 2;
            double stop = wm.wx1;
            const QPen sp = pen(s.p.i("symbol_color"), s.p.i("symbol_pattern"), s.p.d("symbol_linewidth"),
                                s.p.i("symbol_linestyle"));
            QRectF all;
            for (int i = 0; i < x.size(); ++i) {
                if (!(x[i] > 0))
                    continue;
                const double start = stop;
                stop = start + sgn * 2 * M_PI * x[i] / norm;
                const double mid = (start + stop) / 2;
                const double ex = qMax(0.0, e.value(i));
                const QPointF off(ex * r * std::cos(mid), ex * r * std::sin(mid));
                const QPointF cen = c + off;
                const QPointF d = dev(cen);
                const double rp = r * u;
                const int colorIdx = (t == "xycolor" || t == "xycolpat") ? int(std::lround(cc.value(i)))
                                                                         : s.p.i("symbol_fill_color");
                const int patIdx = t == "xycolpat" ? int(std::lround(pt.value(i))) : s.p.i("symbol_fill_pattern");
                const QRectF box(d.x() - rp, d.y() - rp, 2 * rp, 2 * rp);
                all |= box;
                QPainterPath path;
                path.moveTo(d);
                path.arcTo(box, start * 180 / M_PI, (stop - start) * 180 / M_PI);
                path.closeSubpath();
                p->setPen(Qt::NoPen);
                p->setBrush(brush(colorIdx, patIdx));
                p->drawPath(path);
                p->setBrush(Qt::NoBrush);
                p->setPen(sp);
                p->drawPath(path);
                if (s.p.b("avalue")) {
                    QString str;
                    const QString fmt = s.p.s("avalue_format");
                    const int prec = s.p.i("avalue_prec");
                    switch (s.p.i("avalue_type")) {
                    case 1: str = numText(formatNumber(x[i], fmt, prec)); break;
                    case 2: str = numText(formatNumber(e.value(i), fmt, prec)); break;
                    case 3: str = numText(formatNumber(x[i], fmt, prec)) + ", " + numText(formatNumber(e.value(i), fmt, prec)); break;
                    case 4: str = s.strings.value(i); break;
                    case 5: str = numText(formatNumber(cc.value(i), fmt, prec)); break;
                    default: break;
                    }
                    if (!str.isEmpty()) {
                        str = s.p.s("avalue_prepend") + str + s.p.s("avalue_append");
                        const Vec2 ao = s.p.v2("avalue_offset");
                        const double rr = (1 + ex) * r + ao[1];
                        QPointF lp = c + QPointF(std::cos(mid), std::sin(mid)) * rr;
                        text(lp, str, s.p.i("avalue_font"), s.p.d("avalue_char_size"), s.p.i("avalue_color"),
                             s.p.i("avalue_rot"), JustCenter | JustMiddle);
                    }
                }
            }
            HitItem h;
            h.kind = HitItem::Set;
            h.gid = g.id;
            h.sid = s.id;
            h.poly = QPolygonF(all);
            hit(h);
            extend(all);
            break;  // как в Grace — один набор на диаграмму
        }
        drawLegend(g, wm);
    }

    // ---- полярные координаты: x — угол (рад), y — радиус
    void drawPolar(const Graph &g, const WorldMap &wm)
    {
        const QPointF c((wm.vx1 + wm.vx2) / 2, (wm.vy1 + wm.vy2) / 2);
        const double R = qMin(wm.vx2 - wm.vx1, wm.vy2 - wm.vy1) / 2;
        const double r0 = qMin(wm.wy1, wm.wy2), r1 = qMax(wm.wy1, wm.wy2);
        const bool inv = g.p.b("xinvert");
        auto P = [&](double phi, double rho) {
            double rr = (rho - r0) / (r1 - r0) * R;
            double a = inv ? -phi : phi;
            return QPointF(c.x() + rr * std::cos(a), c.y() + rr * std::sin(a));
        };
        if (g.p.i("frame_bg_pattern") != 0) {
            p->setPen(Qt::NoPen);
            p->setBrush(brush(g.p.i("frame_bg_color"), g.p.i("frame_bg_pattern")));
            p->drawEllipse(dev(c), R * u, R * u);
            p->setBrush(Qt::NoBrush);
        }
        const Axis &xa = g.axes[0], &ya = g.axes[1];
        QVector<Tick> tx = xa.p.b("on") ? computeTicks(g, 0) : QVector<Tick>();
        QVector<Tick> ty = ya.p.b("on") ? computeTicks(g, 1) : QVector<Tick>();
        // сетка
        for (const Tick &t : ty) {
            const QString pre = t.major ? "tick_major_" : "tick_minor_";
            if (!ya.p.b(pre + "grid") || t.w <= r0 || t.w > r1)
                continue;
            p->setPen(pen(ya.p.i(pre + "color"), 1, ya.p.d(pre + "linewidth"), ya.p.i(pre + "linestyle")));
            const double rr = (t.w - r0) / (r1 - r0) * R * u;
            p->drawEllipse(dev(c), rr, rr);
        }
        for (const Tick &t : tx) {
            const QString pre = t.major ? "tick_major_" : "tick_minor_";
            if (!xa.p.b(pre + "grid"))
                continue;
            p->setPen(pen(xa.p.i(pre + "color"), 1, xa.p.d(pre + "linewidth"), xa.p.i(pre + "linestyle")));
            vline(c, P(t.w, r1));
        }
        // наборы
        HitItem fh;
        fh.kind = HitItem::GraphFrame;
        fh.gid = g.id;
        for (const DataSet &s : g.sets) {
            if (s.hidden() || s.size() == 0)
                continue;
            const auto &x = s.col(0), &y = s.col(1);
            QVector<QPointF> pd(x.size());
            for (int i = 0; i < x.size(); ++i)
                pd[i] = (std::isfinite(x[i]) && std::isfinite(y.value(i))) ? dev(P(x[i], y.value(i)))
                                                                          : QPointF(NAN, NAN);
            const QPen lp = pen(s.p.i("line_color"), s.p.i("line_pattern"), s.p.d("line_linewidth"),
                                s.p.i("line_linestyle"), true);
            if (s.p.i("fill_type") != 0 && s.p.i("fill_pattern") != 0) {
                QPolygonF poly;
                for (auto q : pd)
                    if (std::isfinite(q.x())) poly << q;
                p->setPen(Qt::NoPen);
                p->setBrush(brush(s.p.i("fill_color"), s.p.i("fill_pattern")));
                p->drawPolygon(poly);
                p->setBrush(Qt::NoBrush);
            }
            if (lp.style() != Qt::NoPen && s.p.i("line_type") != 0) {
                p->setPen(lp);
                QVector<QPointF> run;
                for (auto q : pd) {
                    if (!std::isfinite(q.x())) {
                        drawPolyline(run);
                        run.clear();
                    } else {
                        run << q;
                    }
                }
                drawPolyline(run);
            }
            drawSymbols(g, s, pd, wm);
            HitItem h;
            h.kind = HitItem::Set;
            h.gid = g.id;
            h.sid = s.id;
            for (auto q : pd)
                if (std::isfinite(q.x())) h.poly << q;
            h.polyline = true;
            h.points = true;
            hit(h);
        }
        // обод и радиальная ось
        if (xa.p.b("on") && xa.p.b("bar")) {
            p->setPen(pen(xa.p.i("bar_color"), 1, xa.p.d("bar_linewidth"), xa.p.i("bar_linestyle")));
            p->drawEllipse(dev(c), R * u, R * u);
        }
        if (xa.p.b("on") && xa.p.b("ticklabel")) {
            for (const Tick &t : tx) {
                if (!t.major)
                    continue;
                QPointF d = P(t.w, r1);
                QPointF dir = d - c;
                const double len = std::hypot(dir.x(), dir.y());
                if (len <= 0)
                    continue;
                QPointF lp = d + dir / len * 0.02;
                if (std::fmod(t.w, 2 * M_PI) > 2 * M_PI - 1e-6 && t.w > 0)
                    continue;
                int just = (std::fabs(dir.x()) < 1e-3 * len ? JustCenter : dir.x() > 0 ? JustLeft : JustRight)
                           | (std::fabs(dir.y()) < 1e-3 * len ? JustMiddle : dir.y() > 0 ? JustBottom : JustTop);
                text(lp, numText(t.label), xa.p.i("ticklabel_font"), xa.p.d("ticklabel_char_size"),
                     xa.p.i("ticklabel_color"), 0, just);
            }
        }
        if (ya.p.b("on")) {
            p->setPen(pen(ya.p.i("bar_color"), 1, ya.p.d("bar_linewidth"), ya.p.i("bar_linestyle")));
            if (ya.p.b("bar"))
                vline(c, P(0, r1));
            p->setPen(pen(ya.p.i("tick_major_color"), 1, ya.p.d("tick_major_linewidth"), 1));
            for (const Tick &t : ty) {
                if (t.w < r0 || t.w > r1)
                    continue;
                QPointF q = P(0, t.w);
                const double ts = 0.02 * (t.major ? ya.p.d("tick_major_size") : ya.p.d("tick_minor_size"));
                if (ya.p.b("tick"))
                    vline(q, q + QPointF(0, ts));
                if (t.major && ya.p.b("ticklabel"))
                    text(q - QPointF(0, 0.01), numText(t.label), ya.p.i("ticklabel_font"),
                         ya.p.d("ticklabel_char_size"), ya.p.i("ticklabel_color"), 0, JustCenter | JustTop);
            }
        }
        fh.poly = QPolygonF(QRectF(dev(c.x() - R, c.y() + R), dev(c.x() + R, c.y() - R)));
        extend(fh.poly.boundingRect());
        if (info)
            info->items.append(fh);
    }
};

}  // namespace

QImage patternImage(int pattern, const QColor &c) { return patternImageImpl(pattern, c); }

void renderPage(QPainter *p, const Project &pr, const QRectF &target, RenderInfo *info,
                const RenderOptions &opt)
{
    Painter r(p, pr, target, info, opt);
    r.render();
}

void fitMargins(Project &pr, double pad, int onlyGid)
{
    // Рисуем в маленькую картинку и смотрим, куда вылезли подписи
    const double k = 1.0;
    QImage img(int(pr.pageW * k), int(pr.pageH * k), QImage::Format_ARGB32_Premultiplied);
    img.setDotsPerMeterX(int(72 / 0.0254));
    img.setDotsPerMeterY(int(72 / 0.0254));
    for (int pass = 0; pass < 2; ++pass) {
        img.fill(Qt::white);
        RenderInfo info;
        {
            QPainter p(&img);
            renderPage(&p, pr, QRectF(0, 0, img.width(), img.height()), &info);
        }
        const double S = pr.scale() * k;
        const double W = img.width(), H = img.height();
        bool changed = false;
        for (Graph &g : pr.graphs) {
            if (!g.visible() || (onlyGid >= 0 && g.id != onlyGid))
                continue;
            QRectF e = info.extents.value(g.id);
            if (e.isNull())
                continue;
            const double overL = qMax(0.0, pad * S - e.left()) / S;
            const double overR = qMax(0.0, e.right() - (W - pad * S)) / S;
            const double overB = qMax(0.0, e.bottom() - (H - pad * S)) / S;
            const double overT = qMax(0.0, pad * S - e.top()) / S;
            if (overL + overR + overB + overT < 1e-4)
                continue;
            Vec4 v = g.p.v4("view");
            double x1 = qMin(v[0], v[2]) + overL, x2 = qMax(v[0], v[2]) - overR;
            double y1 = qMin(v[1], v[3]) + overB, y2 = qMax(v[1], v[3]) - overT;
            if (x2 - x1 < 0.1 || y2 - y1 < 0.1)
                continue;
            g.p.setV4("view", x1, y1, x2, y2);
            changed = true;
        }
        if (!changed)
            break;
    }
}

}  // namespace ng
