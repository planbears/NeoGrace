// Шкалы, преобразование координат и расчёт делений — как в Grace.
#pragma once

#include "model.h"

#include <QPointF>
#include <QString>
#include <QVector>

namespace ng {

enum class Scale { Normal, Log, Reciprocal, Logit };
Scale scaleOf(const QString &s);
double fscale(double w, Scale s);
double ifscale(double w, Scale s);

// Мир → вьюпорт для одного графика
struct WorldMap {
    double vx1 = 0.15, vy1 = 0.15, vx2 = 1.15, vy2 = 0.85;
    double wx1 = 0, wy1 = 0, wx2 = 1, wy2 = 1;
    Scale xs = Scale::Normal, ys = Scale::Normal;
    bool xinv = false, yinv = false;
    bool ok = true;
    double fx1 = 0, fx2 = 1, fy1 = 0, fy2 = 1;  // шкалированные границы

    WorldMap() = default;
    explicit WorldMap(const Graph &g);
    double xv(double x) const;
    double yv(double y) const;
    QPointF toView(double x, double y) const { return QPointF(xv(x), yv(y)); }
    double xw(double vx) const;
    double yw(double vy) const;
    bool validX(double x) const;
    bool validY(double y) const;
};

struct Tick {
    double w = 0;
    bool major = true;
    QString label;
};

// Деления оси (ax: 0 x, 1 y, 2 altx, 3 alty)
QVector<Tick> computeTicks(const Graph &g, int ax);

QString formatNumber(double v, const QString &fmt, int prec);
double niceStep(double span, int nticks = 6);
// Хороший шаг делений для диапазона (для автоматики)
void autoTickAxis(Graph &g, int ax);

}  // namespace ng
