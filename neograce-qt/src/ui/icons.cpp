#include "icons.h"

#include "iconpaths.h"
#include "render.h"

#include <QApplication>
#include <QHash>
#include <QPainter>
#include <QPalette>
#include <QSvgRenderer>
#include <cmath>

namespace ng {

static QHash<QString, QIcon> &cache()
{
    static QHash<QString, QIcon> c;
    return c;
}

void clearIconCache() { cache().clear(); }

QPixmap iconPixmap(const QString &name, const QColor &color, int px, double dpr)
{
    const QString body = iconPaths().value(name, iconPaths().value("page"));
    const QString svg = QString("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"24\" height=\"24\" viewBox=\"0 0 24 24\" "
                                "fill=\"none\" stroke=\"%1\" stroke-width=\"1.7\" stroke-linecap=\"round\" "
                                "stroke-linejoin=\"round\">%2</svg>")
                            .arg(color.name(), body);
    QSvgRenderer r(svg.toUtf8());
    QPixmap pm(int(px * dpr), int(px * dpr));
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    r.render(&p, QRectF(0, 0, px * dpr, px * dpr));
    p.end();
    pm.setDevicePixelRatio(dpr);
    return pm;
}

QIcon icon(const QString &name, const QColor &color)
{
    const QString key = name + color.name();
    auto it = cache().find(key);
    if (it != cache().end())
        return *it;
    QIcon ic;
    QColor dis = color;
    dis.setAlphaF(0.35);
    for (int px : {16, 20, 24, 32}) {
        ic.addPixmap(iconPixmap(name, color, px), QIcon::Normal);
        ic.addPixmap(iconPixmap(name, dis, px), QIcon::Disabled);
    }
    cache().insert(key, ic);
    return ic;
}

QIcon icon(const QString &name)
{
    QColor c = qApp->palette().color(QPalette::WindowText);
    // чуть мягче чистого чёрного/белого
    c = c.lightness() < 128 ? QColor(58, 58, 60) : QColor(225, 225, 230);
    return icon(name, c);
}

static QPixmap canvas(int w, int h)
{
    QPixmap pm(w * 2, h * 2);
    pm.fill(Qt::transparent);
    pm.setDevicePixelRatio(2.0);
    return pm;
}

QIcon symbolIcon(int type, const QColor &c)
{
    QPixmap pm = canvas(18, 18);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(c, 1.3));
    p.setBrush(Qt::NoBrush);
    const QPointF d(9, 9);
    const double r = 5.5;
    switch (type) {
    case 0: p.setPen(QPen(c.lighter(160), 1)); p.drawLine(QPointF(4, 9), QPointF(14, 9)); break;
    case 1: p.drawEllipse(d, r, r); break;
    case 2: p.drawRect(QRectF(9 - r * .85, 9 - r * .85, 2 * r * .85, 2 * r * .85)); break;
    case 3: p.drawPolygon(QPolygonF({{9, 9 - r}, {9 - r, 9}, {9, 9 + r}, {9 + r, 9}})); break;
    case 4: p.drawPolygon(QPolygonF({{9, 3}, {3.5, 13}, {14.5, 13}})); break;
    case 5: p.drawPolygon(QPolygonF({{3, 9}, {13, 3.5}, {13, 14.5}})); break;
    case 6: p.drawPolygon(QPolygonF({{9, 15}, {3.5, 5}, {14.5, 5}})); break;
    case 7: p.drawPolygon(QPolygonF({{15, 9}, {5, 3.5}, {5, 14.5}})); break;
    case 8: p.drawLine(QPointF(3.5, 9), QPointF(14.5, 9)); p.drawLine(QPointF(9, 3.5), QPointF(9, 14.5)); break;
    case 9: p.drawLine(QPointF(5, 5), QPointF(13, 13)); p.drawLine(QPointF(5, 13), QPointF(13, 5)); break;
    case 10:
        p.drawLine(QPointF(3.5, 9), QPointF(14.5, 9)); p.drawLine(QPointF(9, 3.5), QPointF(9, 14.5));
        p.drawLine(QPointF(5, 5), QPointF(13, 13)); p.drawLine(QPointF(5, 13), QPointF(13, 5));
        break;
    case 11: {
        QFont f = p.font();
        f.setPixelSize(12);
        p.setFont(f);
        p.drawText(QRectF(0, 0, 18, 18), Qt::AlignCenter, "A");
        break;
    }
    }
    p.end();
    return QIcon(pm);
}

QIcon lineStyleIcon(int style, const QColor &c, int w)
{
    QPixmap pm = canvas(w, 14);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    if (style == 0) {
        QPen pn(c.lighter(170), 1, Qt::DotLine);
        p.setPen(pn);
        p.drawText(QRectF(0, 0, w, 14), Qt::AlignCenter, "—");
    } else {
        QPen pn(c, 1.6);
        auto d = dashPattern(style);
        if (!d.isEmpty()) {
            pn.setDashPattern(d);
            pn.setCapStyle(Qt::FlatCap);
        }
        p.setPen(pn);
        p.drawLine(QPointF(2, 7), QPointF(w - 2, 7));
    }
    p.end();
    return QIcon(pm);
}

QIcon patternIcon(int pattern, const QColor &c)
{
    QPixmap pm = canvas(24, 14);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QRectF r(1.5, 1.5, 21, 11);
    if (pattern == 0) {
        p.setPen(QPen(QColor(150, 150, 150), 1, Qt::DashLine));
        p.drawRect(r);
    } else {
        QBrush b = pattern == 1 ? QBrush(c) : QBrush(patternImage(pattern, c));
        if (pattern != 1)
            b.setTransform(QTransform::fromScale(0.5, 0.5));
        p.setPen(QPen(QColor(120, 120, 120), 0.8));
        p.setBrush(b);
        p.drawRect(r);
    }
    p.end();
    return QIcon(pm);
}

QIcon colorIcon(const QColor &c)
{
    QPixmap pm = canvas(16, 16);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(0, 0, 0, 60), 1));
    p.setBrush(c);
    p.drawRoundedRect(QRectF(1.5, 1.5, 13, 13), 3.5, 3.5);
    p.end();
    return QIcon(pm);
}

}  // namespace ng
