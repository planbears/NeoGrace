// Отрисовка проекта Grace через QPainter.
//
// Масштабы взяты из исходников Grace: символ размера 1 — радиус 0.01 от
// короткой стороны страницы, шрифт размера 1 — 0.028, линия толщины 1 —
// 0.0015, засечка размера 1 — 0.02.  Поэтому графики пропорционально такие
// же, как в xmgrace, но со сглаживанием и современными шрифтами.
#pragma once

#include "axes.h"
#include "model.h"

#include <QHash>
#include <QImage>
#include <QPainterPath>
#include <QPolygonF>
#include <QRectF>
#include <QVector>

class QPainter;

namespace ng {

struct HitItem {
    enum Kind { GraphFrame, Set, Legend, Title, Subtitle, AxisLabel, TickLabels, Object };
    Kind kind = GraphFrame;
    int gid = -1, sid = -1, obj = -1, axis = -1;
    QPolygonF poly;      // контур (прямоугольник/многоугольник) или полилиния
    bool polyline = false;  // для наборов: линия или точки
    bool points = false;
};

struct RenderInfo {
    QRectF page;           // страница на устройстве
    double unit = 1;       // пикселей на единицу вьюпорта
    QVector<HitItem> items;  // в порядке рисования (последний — сверху)
    QHash<int, QRectF> frames;   // gid → рамка графика на устройстве
    QHash<int, QRectF> extents;  // gid → всё нарисованное для графика
    QHash<int, WorldMap> maps;

    QPointF toDev(double vx, double vy) const
    {
        return QPointF(page.left() + vx * unit, page.bottom() - vy * unit);
    }
    QPointF toView(QPointF d) const
    {
        return QPointF((d.x() - page.left()) / unit, (page.bottom() - d.y()) / unit);
    }
};

struct RenderOptions {
    bool typographicMinus = true;  // «−» вместо дефиса в числах
    bool drawBackground = true;
};

// Нарисовать страницу проекта в прямоугольник target (координаты устройства).
void renderPage(QPainter *p, const Project &pr, const QRectF &target,
                RenderInfo *info = nullptr, const RenderOptions &opt = RenderOptions());

// Подогнать поля графиков, чтобы подписи не вылезали за край страницы.
void fitMargins(Project &pr, double pad = 0.02, int onlyGid = -1);

// Ширина линии Grace → пиксели для единицы вьюпорта u
double lineWidthPx(double lw, double unit);
// Штриховка
QVector<qreal> dashPattern(int style);
// Узор заливки Grace 16×16
QImage patternImage(int pattern, const QColor &c);

}  // namespace ng
