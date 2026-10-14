// Операции над проектом: оформление, автомасштаб, расстановка графиков,
// преобразования данных, регрессия и нелинейная аппроксимация.
#pragma once

#include "agrio.h"
#include "model.h"

#include <QString>
#include <QHash>
#include <QPair>
#include <QVector>

namespace ng {

Project newProject(bool modern = true);
// Стили оформления: modern paper classic journal presentation dark bw grid minimal soft
struct StyleDef {
    QString key;
    const char *name;  // по-русски; в интерфейсе — через T()
    const char *tip;
};
const QVector<StyleDef> &styleDefs();
void applyStyle(Project &pr, const QString &name);
// Раскрасить наборы по порядку цветами шкалы: viridis, inferno, blues
void gradientColors(Project &pr, const QVector<QPair<int, int>> &sets, const QString &name = "viridis");
void styleImported(Project &pr, Graph &g);

bool setBounds(const DataSet &s, const Graph *g, double &xmin, double &xmax, double &ymin, double &ymax);
// axes: "xy", "x" или "y"; sets — только эти наборы (пусто — все видимые)
bool autoscaleGraph(Graph &g, const QString &axes = "xy", const QVector<int> &sets = {});
void updateTicksForWorld(Graph &g);
void arrangeGraphs(Project &pr, int rows, int cols, double left = 0.15, double right = 0.05,
                   double bottom = 0.12, double top = 0.08, double hgap = 0.1, double vgap = 0.1,
                   bool addMissing = true);

// ---- данные
bool applyFormula(Graph &g, DataSet &s, const QString &text, QString *error);
int createFromFormula(Graph &g, double xmin, double xmax, int npts, const QString &yexpr,
                      QString *error, const QString &xexpr = QString());
int addTableAsSets(Graph &g, const Table &t, const QString &mode, const QString &type = "xy",
                   const QVector<int> &columns = {});
int duplicateSet(Graph &g, int sid);

struct FitResult {
    bool ok = false;
    QString message;  // для показа пользователю
    QVector<double> params;
    double r2 = 0, chi2 = 0;
    int newSet = -1;
};

// kind: linear, poly, exp, power, log
FitResult regression(Graph &g, int sid, const QString &kind, int degree = 1, int npts = 200);
FitResult nonlinearFit(Graph &g, int sid, const QString &expr, const QVector<double> &p0,
                       int maxIter = 200, int npts = 300, double xmin = 0, double xmax = 0);

int runningAverage(Graph &g, int sid, int window);
int derivative(Graph &g, int sid);
int integral(Graph &g, int sid, double *total = nullptr);
void sortSet(DataSet &s, bool byY, bool descending);
int fourier(Graph &g, int sid, bool power = false);
int histogram(Graph &g, int sid, int bins, double lo, double hi, bool normalize = false);
int interpolate(Graph &g, int sid, int npts, bool spline);
QString statistics(const DataSet &s);

// Заливка между кривой и осью X: mode 0 — убрать, 1 — до y = 0,
// 2 — до нижнего края графика, 3 — до верхнего.  Цвет — светлый оттенок
// цвета линии (добавляется в палитру).
void fillToAxis(Project &pr, int gid, int sid, int mode);
int fillToAxisMode(const DataSet &s);  // текущий режим (или -1, если заливка другая)
// Один цвет для всех элементов набора (линия, символы, погрешности)
void setSetColor(DataSet &s, int color);
// ---- порядок слоёв (что рисуется поверх).  В Grace порядок задаётся
// номерами, поэтому при перестановке номера перераспределяются по порядку.
enum LayerMove { LayerBack = -2, LayerBackward = -1, LayerForward = 1, LayerFront = 2 };
// Новый порядок списка: выделенные элементы сдвигаются на шаг или в край
QVector<int> layerOrder(const QVector<int> &order, const QVector<int> &selected, int move);
// Переставить наборы графика (order — все id в новом порядке); вернуть «старый id → новый»
QHash<int, int> reorderSets(Graph &g, const QVector<int> &order);
// То же для графиков (ссылки надписей на графики обновляются)
QHash<int, int> reorderGraphs(Project &pr, const QVector<int> &order);
// Перенести наборы (gid, sid) в график target перед позицией pos (−1 — в конец)
QHash<QPair<int, int>, int> moveSetsToGraph(Project &pr, const QVector<QPair<int, int>> &sets, int target, int pos);

// ---- наборы, набранные текстом:  «20.0 -10.0⏎20.0 10.0» — отрезок
struct TextData {
    QVector<Table> blocks;  // блоки разделяются строкой «&» или пустой строкой
    QStringList errors;     // «строка N: …» для непонятых строк
    QStringList formulas;   // строки «y = sin(x)» — функции от x
    int points = 0, maxCols = 0;
};
TextData parseDataText(const QString &text);
// Наборы по формулам «y = …» на отрезке [x1, x2] из n точек
QVector<int> addFormulaSets(Graph &g, const QStringList &formulas, double x1, double x2, int n,
                            const QString &legend = QString());
// type: "auto" (2 столбца — xy, 1 — y по номеру точки, больше — X и несколько Y)
// или тип Grace (xy, xydy, …).  Вернуть номера новых наборов.
QVector<int> addSetsFromText(Graph &g, const TextData &d, const QString &type, const QString &legend = QString());
// Заменить данные набора первым блоком текста
bool replaceSetFromText(DataSet &s, const TextData &d, const QString &type);
QString setToText(const DataSet &s);
// Расширить границы графика, если новые наборы в них не помещаются
void ensureVisible(Graph &g, const QVector<int> &sets);

// Разные цвета по порядку для нескольких наборов
void distinctColors(Project &pr, const QVector<QPair<int, int>> &sets);

}  // namespace ng
