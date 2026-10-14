// Арифметика над наборами: A + B, A − B, A × B, A ÷ B, A ^ B, где B — другой
// набор или число; сумма и среднее нескольких наборов; действия над одним
// набором (−y, |y|, 1/y, нормировка, вычесть минимум, поменять X и Y).
//
// Если сетки X наборов различаются, второй набор линейно интерполируется на
// сетку X первого; точки первого вне диапазона второго отбрасываются.
// Симметричные погрешности по Y (xydy, bardy, …) распространяются по
// обычным правилам (сумма квадратов относительных/абсолютных ошибок).
#pragma once

#include "model.h"

#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>

namespace ng {

enum class MathOp { Add, Sub, Mul, Div, Pow };
QString mathOpSymbol(MathOp op);  // + − × ÷ ^

// Роль столбца в наборе данного типа:
// 'x' — координата X, 'y' — значение Y (в т.ч. hi/lo/медиана),
// 'X' — погрешность или приращение по X, 'Y' — по Y, 'o' — прочее (z, размер, цвет…)
QChar columnRole(const QString &type, int col);

// Набор op число.  onX — действовать на X, иначе на все значения Y.
bool mathWithNumber(DataSet &s, MathOp op, double c, bool onX, QString *error = nullptr);

struct CombineInfo {
    int kept = 0;          // точек в результате
    int dropped = 0;       // точек первого набора вне диапазона X второго
    int nonFinite = 0;     // точек с результатом ±∞/NaN (деление на 0…) — отброшены
    bool interpolated = false;
};
// A op B → out (тип xy/bar или xydy/bardy, если есть погрешности)
bool combineSets(const DataSet &a, const DataSet &b, MathOp op, DataSet &out, CombineInfo *info = nullptr,
                 QString *error = nullptr);
// Сумма (mean=false) или среднее нескольких наборов на сетке X первого.
// withStd — для среднего записать стандартное отклонение как погрешность (xydy).
bool aggregateSets(const QVector<const DataSet *> &sets, bool mean, bool withStd, DataSet &out,
                   CombineInfo *info = nullptr, QString *error = nullptr);

// Действие над одним набором: neg abs inv normmax normarea submin swapxy
bool unarySetOp(DataSet &s, const QString &op, QString *error = nullptr);
const QStringList &unarySetOps();

// ---- формула из наборов: «s0.y + s1.y - s2.y», «(s0 + s1) / 2», «g1.s0.y * 2»,
// «s0.y - 0.5*s1.y».  sN без столбца — это sN.y; без gM — набор графика home.
// Можно писать × ÷ − и «y = …».  Сетка X — первого набора формулы (или grid);
// остальные наборы пересчитываются на неё линейной интерполяцией, точки вне
// их диапазона пропускаются.  Переменная x — сама сетка.
QString normalizeSetExpression(const QString &text);
bool setExpression(const Project &pr, int home, const QString &text, DataSet &out, CombineInfo *info = nullptr,
                   QString *error = nullptr, int gridG = -1, int gridS = -1,
                   QVector<QPair<int, int>> *refs = nullptr);
// Готовые формулы для выделенных наборов: sum — «s0.y + s1.y + …»,
// diff — «s0.y - s1.y - …», mean — «(s0.y + s1.y + …) / n»
QString setExpressionFor(const QVector<QPair<int, int>> &sets, int home, const QString &kind);

// ---- для интерфейса: одна операция над проектом
struct SetMathSpec {
    // expr | add sub mul div pow | sum mean | neg abs inv normmax normarea submin swapxy
    QString op = "add";
    QString expr;                // для expr; ga — график, к которому относятся «sN»
    int gridG = -1, gridS = -1;  // для expr: чья сетка X (−1 — первого набора формулы)
    int ga = 0, sa = 0;          // первый набор (A)
    bool withSet = true;         // второй операнд — набор (иначе число)
    int gb = 0, sb = 1;          // второй набор (B)
    double number = 1;           // или число
    bool onX = false;            // число применяется к X
    QVector<QPair<int, int>> many;  // для sum/mean — наборы; для действий с числом и
                                    // над одним набором — все, к кому применить (пусто — только A)
    bool withStd = true;         // среднее ± σ
    bool replace = false;        // изменить сами наборы вместо создания новых
};
struct SetMathResult {
    bool ok = false;
    QVector<QPair<int, int>> sets;  // новые или изменённые наборы (граф, набор)
    QString message;
};
bool isBinaryMathOp(const QString &op);   // add sub mul div pow
bool isAggregateOp(const QString &op);    // sum mean
SetMathResult runSetMath(Project &pr, const SetMathSpec &spec);
// Подпись результата («S0 + S1», «S0 × 2»…)
QString setMathLabel(const Project &pr, const SetMathSpec &spec, int ga, int sa);

}  // namespace ng
