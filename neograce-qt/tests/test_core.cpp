// Автотесты ядра NeoGrace: разбор, запись, формулы, аппроксимация, отрисовка.
// Запуск: neograce_tests [папка-для-картинок]
#include "agrio.h"
#include "axes.h"
#include "export.h"
#include "expr.h"
#include "gracetext.h"
#include "i18n.h"
#include "ops.h"
#include "setmath.h"

#include <QSet>
#include "render.h"

#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <cmath>
#include <cstdio>

using namespace ng;

static int g_fail = 0, g_ok = 0;
#define CHECK(cond)                                                                        \
    do {                                                                                   \
        if (cond) {                                                                        \
            ++g_ok;                                                                        \
        } else {                                                                           \
            ++g_fail;                                                                      \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
        }                                                                                  \
    } while (0)
#define NEAR(a, b, tol) CHECK(std::fabs((a) - (b)) <= (tol))

static void testProps()
{
    QVariant v;
    CHECK(parseValue(PT::OnOff, "on", {}, v) && v.toBool());
    CHECK(parseValue(PT::Str, "\"a \\\"b\\\" c\"", {}, v) && v.toString() == "a \"b\" c");
    CHECK(quoteString("a \"b\"") == "\"a \\\"b\\\"\"");
    CHECK(parseValue(PT::Quad, "0.1, 0.2, 0.3, 0.4", {}, v));
    CHECK(formatValue(PT::Quad, v) == "0.100000, 0.200000, 0.300000, 0.400000");
    CHECK(parseValue(PT::Word, "logarithmic", kScaleTypes, v) && v.toString() == "Logarithmic");
    CHECK(!parseValue(PT::Word, "bogus", kScaleTypes, v));
    CHECK(normalizeCommand("  s0   LINE color 2 \"A  B\"") == "s0 line color 2 \"A  B\"");
}

static void testParseMinimal()
{
    const char *txt =
        "# Grace project file\n@version 50122\n@page size 792, 612\n"
        "@map color 2 to (255, 0, 0), \"red\"\n"
        "@g0 on\n@with g0\n@    world 0, -1, 10, 1\n@    view 0.15, 0.15, 1.15, 0.85\n"
        "@    title \"Hello \\xa\\f{}\"\n@    xaxes scale Normal\n@    xaxis  tick major 2\n"
        "@    xaxis  ticklabel format decimal\n@    xaxis  ticklabel prec 1\n"
        "@    s0 symbol 2\n@    s0 line color 2\n@    s0 legend  \"sin\"\n"
        "@    s0 some unknown thing 5\n@    frobnicate 1\n"
        "@target G0.S0\n@type xydy\n0 0 0.1\n1 0.5 0.2\n2 1 0.1\n&\n";
    Project pr = parseAgr(QString::fromLatin1(txt));
    CHECK(pr.graphs.size() == 1);
    const Graph &g = pr.graphs[0];
    Vec4 w = g.p.v4("world");
    NEAR(w[2], 10.0, 1e-12);
    NEAR(w[1], -1.0, 1e-12);
    CHECK(g.p.s("title") == "Hello \\xa\\f{}");
    CHECK(g.axes[0].p.d("tick_major") == 2.0);
    CHECK(g.axes[0].p.s("ticklabel_format") == "decimal");
    CHECK(g.sets.size() == 1);
    const DataSet &s = g.sets[0];
    CHECK(s.type() == "xydy");
    CHECK(s.size() == 3);
    NEAR(s.col(2)[1], 0.2, 1e-12);
    CHECK(s.p.i("symbol") == 2);
    CHECK(s.p.i("line_color") == 2);
    CHECK(s.p.s("legend") == "sin");
    CHECK(s.extra.size() == 1);
    CHECK(g.extra.contains("@    frobnicate 1"));
    CHECK(stripEscapes(g.p.s("title")) == "Hello α");

    // запись и повторное чтение
    QString out = writeAgr(pr);
    CHECK(out.contains("@    s0 some unknown thing 5"));
    CHECK(out.contains("@    frobnicate 1"));
    Project pr2 = parseAgr(out);
    CHECK(pr2.graphs.size() == 1);
    CHECK(pr2.graphs[0].sets[0].size() == 3);
    CHECK(pr2.graphs[0].p.all() == pr.graphs[0].p.all());
    CHECK(pr2.graphs[0].sets[0].p.all() == pr.graphs[0].sets[0].p.all());
    CHECK(writeAgr(pr2) == out);  // стабильность
}

static void testXvg()
{
    const char *txt =
        "# GROMACS\n@    title \"RMSD\"\n@    xaxis  label \"Time (ns)\"\n@TYPE xy\n"
        "@ s0 legend \"a\"\n@ s1 legend \"b\"\n0 1 2\n1 2 3\n2 3 5\n";
    Project pr = parseAgr(QString::fromLatin1(txt), false);
    CHECK(pr.graphs.size() == 1);
    const Graph &g = pr.graphs[0];
    CHECK(g.sets.size() == 2);
    CHECK(g.sets[0].p.s("legend") == "a");
    CHECK(g.sets[1].size() == 3);
    NEAR(g.sets[1].col(1)[2], 5.0, 0);
    CHECK(g.axes[0].p.s("label") == "Time (ns)");
}

static void testExpr()
{
    QVector<double> x{0, 1, 2, 3}, y;
    CHECK(evalOnArray("2*x^2 + 1", x, y));
    NEAR(y[3], 19.0, 1e-12);
    CHECK(evalOnArray("-2^2", x, y));
    NEAR(y[0], -4.0, 1e-12);
    CHECK(evalOnArray("x > 1 ? sin(pi/2) : max(x, 0.5, -1)", x, y));
    NEAR(y[0], 0.5, 1e-12);
    NEAR(y[2], 1.0, 1e-12);
    CHECK(evalOnArray("1e-3*x + 2E2", x, y));
    NEAR(y[1], 200.001, 1e-9);
    QString err;
    CHECK(!evalOnArray("sin(x", x, y, &err) && !err.isEmpty());
    CHECK(!evalOnArray("foo(x)", x, y, &err));
    QVector<Expr::Assignment> as;
    CHECK(Expr::splitAssignments("x = x*2; y = x + 1", as, &err) && as.size() == 2);

    Graph g(0);
    DataSet &s = g.newSet("xy");
    s.cols = {{0, 1, 2}, {5, 6, 7}};
    CHECK(applyFormula(g, s, "x = x*2; y = x + 1", &err));
    NEAR(g.sets[0].col(0)[2], 4.0, 0);
    NEAR(g.sets[0].col(1)[2], 5.0, 0);
    DataSet &s2 = g.newSet("xy");
    s2.cols = {{0, 1, 2}, {0, 0, 0}};
    CHECK(applyFormula(g, s2, "y = s0.y * 10", &err));
    NEAR(g.sets[1].col(1)[1], 30.0, 1e-12);
}

static void testFormat()
{
    CHECK(formatNumber(0.30000000000000004, "general", 5) == "0.3");
    CHECK(formatNumber(1.5, "decimal", 2) == "1.50");
    CHECK(formatNumber(-0.0, "decimal", 1) == "0.0");
    CHECK(formatNumber(1000, "power", 0) == "10\\S3\\N");
    CHECK(formatNumber(2048, "computing", 3) == "2K");
    CHECK(formatNumber(1500, "engineering", 1) == "1.5 k");
    CHECK(formatNumber(2451545.0, "yymmdd", 0) == "2000-01-01");
}

static void testTicks()
{
    Graph g(0);
    g.p.setV4("world", 0, 0, 10, 1);
    g.axes[0].p.set("tick_major", 2.0);
    g.axes[0].p.set("tick_minor_ticks", 1);
    auto t = computeTicks(g, 0);
    int maj = 0, mn = 0;
    for (auto &k : t) (k.major ? maj : mn)++;
    CHECK(maj == 6);
    CHECK(mn == 5);
    g.p.set("xscale", "Logarithmic");
    g.p.setV4("world", 1, 0, 1000, 1);
    g.axes[0].p.set("tick_major", 10.0);
    g.axes[0].p.set("tick_minor_ticks", 8);
    t = computeTicks(g, 0);
    maj = mn = 0;
    for (auto &k : t) (k.major ? maj : mn)++;
    CHECK(maj == 4);
    CHECK(mn == 24);
    WorldMap wm(g);
    NEAR(wm.xv(10), 0.15 + 1.0 / 3.0, 1e-9);
    NEAR(wm.xw(wm.xv(37.0)), 37.0, 1e-9);
}

static void testOps()
{
    Graph g(0);
    DataSet &s = g.newSet("xy");
    QVector<double> x, y;
    for (int i = 0; i < 50; ++i) {
        x << i * 0.1;
        y << 3.0 * std::exp(-0.7 * x.last()) + 0.5;
    }
    s.cols = {x, y};
    FitResult r = nonlinearFit(g, s.id, "a0*exp(-a1*x) + a2", {1, 1, 0});
    CHECK(r.ok);
    NEAR(r.params[0], 3.0, 1e-5);
    NEAR(r.params[1], 0.7, 1e-5);
    NEAR(r.params[2], 0.5, 1e-5);
    CHECK(g.sets.size() == 2);

    Graph g2(0);
    DataSet &p = g2.newSet("xy");
    p.cols = {{0, 1, 2, 3, 4}, {1, 3, 5, 7, 9}};
    FitResult lr = regression(g2, p.id, "linear");
    CHECK(lr.ok);
    NEAR(lr.params[0], 1.0, 1e-10);
    NEAR(lr.params[1], 2.0, 1e-10);
    NEAR(lr.r2, 1.0, 1e-12);
    FitResult pr2 = regression(g2, 0, "poly", 2);
    CHECK(pr2.ok);
    NEAR(pr2.params[2], 0.0, 1e-9);

    CHECK(autoscaleGraph(g2));
    Vec4 w = g2.p.v4("world");
    CHECK(w[0] <= 0 && w[2] >= 4 && w[1] <= 1 && w[3] >= 9);

    double total = 0;
    CHECK(integral(g2, 0, &total) >= 0);
    NEAR(total, 20.0, 1e-12);
    int d = derivative(g2, 0);
    NEAR(g2.set(d)->col(1)[2], 2.0, 1e-12);
    int h = histogram(g2, 0, 4, 0, 10);
    CHECK(g2.set(h)->type() == "bar");
    int sp = interpolate(g2, 0, 9, true);
    NEAR(g2.set(sp)->col(1)[4], 5.0, 1e-9);

    Project pr = newProject();
    arrangeGraphs(pr, 2, 2);
    CHECK(pr.graphs.size() == 4);
}

static void testTextData()
{
    // пример пользователя: две строки — вертикальный отрезок x = 20
    TextData d = parseDataText("20.0 -10.0\n20.0 10.0\n");
    CHECK(d.blocks.size() == 1 && d.points == 2 && d.maxCols == 2 && d.errors.isEmpty());
    Project pr = newProject();
    Graph &g = pr.graphs[0];
    const auto ids = addSetsFromText(g, d, "auto", "line");
    CHECK(ids.size() == 1 && g.sets.size() == 1);
    const DataSet &s = *g.set(ids[0]);
    CHECK(s.type() == "xy" && s.size() == 2);
    CHECK(s.col(0) == (QVector<double>{20, 20}) && s.col(1) == (QVector<double>{-10, 10}));
    CHECK(s.p.s("legend") == "line");
    ensureVisible(g, ids);
    const Vec4 w = g.p.v4("world");
    CHECK(w[0] < 20 && w[2] > 20 && w[1] <= -10 && w[3] >= 10);  // вырожденный X тоже виден

    // разделители: табуляция, «;», запятая; пустая строка и «&» — новые наборы; комментарии
    d = parseDataText("# t\n0\t1\n1;2\n\n5, 6\n&\n7 8\nabc\n");
    CHECK(d.blocks.size() == 3 && d.points == 4 && d.errors.size() == 1);
    CHECK(d.errors.value(0).contains("8"));
    CHECK(addSetsFromText(g, d, "auto").size() == 3 && g.sets.size() == 4);

    // точка за пределами масштаба расширяет его
    d = parseDataText("1000 1000");
    const auto far = addSetsFromText(g, d, "auto");
    CHECK(g.set(far[0])->p.i("symbol") == 1);  // одиночная точка — с символом
    ensureVisible(g, far);
    CHECK(g.p.v4("world")[2] >= 1000 && g.p.v4("world")[3] >= 1000);

    // столбцы: один — Y по номеру, три в «авто» — два Y, три для xydy — погрешность
    d = parseDataText("5\n6\n7");
    auto one = addSetsFromText(g, d, "auto");
    CHECK(g.set(one[0])->col(0) == (QVector<double>{0, 1, 2}));
    d = parseDataText("1 2 3\n2 3 4");
    CHECK(addSetsFromText(g, d, "auto").size() == 2);
    auto dy = addSetsFromText(g, d, "xydy");
    CHECK(dy.size() == 1 && g.set(dy[0])->type() == "xydy" && g.set(dy[0])->col(2) == (QVector<double>{3, 4}));

    // замена данных и обратное преобразование в текст
    DataSet &r = *g.set(ids[0]);
    CHECK(replaceSetFromText(r, parseDataText("0 0\n1 1\n2 4"), "auto"));
    CHECK(r.size() == 3 && r.type() == "xy" && r.p.s("legend") == "line");
    CHECK(setToText(r) == "0 0\n1 1\n2 4\n");
    CHECK(!replaceSetFromText(r, parseDataText("\n\n"), "auto"));
    // функции: «y = …» вместе с числами
    d = parseDataText("y = 2*x + 1\nf(x) = sin(x)\n1 2\n3 4\nY=(\n");
    CHECK(d.formulas == (QStringList{"2*x + 1", "sin(x)"}) && d.points == 2 && d.errors.size() == 1);
    const auto fs = addFormulaSets(g, d.formulas, 0, 10, 11, "f");
    CHECK(fs.size() == 2 && g.set(fs[0])->size() == 11 && g.set(fs[0])->col(1)[10] == 21);
    CHECK(g.set(fs[1])->p.s("legend") == "f 2");
    // запись и чтение .agr
    Project back = parseAgr(writeAgr(pr));
    CHECK(back.graphs[0].sets.size() == g.sets.size());
}

static DataSet mkSet(const QString &type, QVector<QVector<double>> cols, int id = 0)
{
    DataSet s(id, type);
    s.cols = cols;
    return s;
}

static void testSetMath()
{
    const DataSet a = mkSet("xy", {{0, 1, 2, 3}, {1, 2, 3, 4}});
    const DataSet b = mkSet("xy", {{0, 1, 2, 3}, {10, 20, 30, 40}});
    DataSet out;
    CombineInfo inf;
    CHECK(combineSets(a, b, MathOp::Add, out, &inf) && !inf.interpolated && inf.kept == 4);
    CHECK(out.type() == "xy" && out.col(1) == (QVector<double>{11, 22, 33, 44}));
    CHECK(combineSets(a, b, MathOp::Sub, out) && out.col(1) == (QVector<double>{-9, -18, -27, -36}));
    CHECK(combineSets(b, a, MathOp::Div, out) && out.col(1) == (QVector<double>{10, 10, 10, 10}));
    CHECK(combineSets(a, a, MathOp::Mul, out) && out.col(1) == (QVector<double>{1, 4, 9, 16}));

    // другая сетка X: B интерполируется, точки A вне диапазона B отбрасываются
    const DataSet c = mkSet("xy", {{3, 0.5, 2.5}, {2.5, 0, 2}});  // y = x - 0.5, не по порядку
    CHECK(combineSets(a, c, MathOp::Add, out, &inf));
    CHECK(inf.interpolated && inf.dropped == 1 && inf.kept == 3);
    CHECK(out.col(0) == (QVector<double>{1, 2, 3}));
    CHECK(out.col(1) == (QVector<double>{2.5, 4.5, 6.5}));
    // деление на ноль — точка пропускается
    const DataSet z = mkSet("xy", {{0, 1, 2, 3}, {1, 0, 1, 1}});
    CHECK(combineSets(a, z, MathOp::Div, out, &inf) && inf.nonFinite == 1 && out.size() == 3);
    // нет общего диапазона
    QString err;
    const DataSet far = mkSet("xy", {{10, 11}, {1, 1}});
    CHECK(!combineSets(a, far, MathOp::Add, out, &inf, &err) && !err.isEmpty());

    // погрешности: (y ± 3) + (y ± 4) → ± 5
    const DataSet ea = mkSet("xydy", {{0, 1}, {1, 1}, {3, 3}});
    const DataSet eb = mkSet("xydy", {{0, 1}, {2, 2}, {4, 4}});
    CHECK(combineSets(ea, eb, MathOp::Add, out) && out.type() == "xydy");
    NEAR(out.col(2)[0], 5.0, 1e-12);
    CHECK(combineSets(ea, eb, MathOp::Mul, out));  // √((2·3)² + (1·4)²)
    NEAR(out.col(2)[0], std::sqrt(52.0), 1e-12);
    // столбики остаются столбиками
    CHECK(combineSets(mkSet("bar", {{0, 1}, {1, 2}}), a, MathOp::Add, out) && out.type() == "bar");

    // с числом: Y, погрешности масштабируются; X
    DataSet d = ea;
    CHECK(mathWithNumber(d, MathOp::Mul, -2, false));
    CHECK(d.col(1) == (QVector<double>{-2, -2}) && d.col(2) == (QVector<double>{6, 6}) && d.col(0) == (QVector<double>{0, 1}));
    CHECK(mathWithNumber(d, MathOp::Add, 10, false) && d.col(1) == (QVector<double>{8, 8}) && d.col(2)[0] == 6);
    CHECK(mathWithNumber(d, MathOp::Mul, 1000, true) && d.col(0) == (QVector<double>{0, 1000}));
    CHECK(!mathWithNumber(d, MathOp::Div, 0, false, &err));
    d = a;
    CHECK(mathWithNumber(d, MathOp::Pow, 2, false) && d.col(1) == (QVector<double>{1, 4, 9, 16}));
    DataSet hl = mkSet("xyhilo", {{0}, {5}, {1}, {2}, {3}});
    CHECK(mathWithNumber(hl, MathOp::Add, 1, false) && hl.col(4)[0] == 4 && hl.col(0)[0] == 0);

    // над одним набором
    d = a;
    CHECK(unarySetOp(d, "neg") && d.col(1) == (QVector<double>{-1, -2, -3, -4}));
    CHECK(unarySetOp(d, "abs") && d.col(1) == (QVector<double>{1, 2, 3, 4}));
    CHECK(unarySetOp(d, "normmax") && d.col(1) == (QVector<double>{0.25, 0.5, 0.75, 1}));
    d = a;
    CHECK(unarySetOp(d, "normarea"));  // ∫ = 7.5
    NEAR(d.col(1)[3], 4 / 7.5, 1e-12);
    d = a;
    CHECK(unarySetOp(d, "submin") && d.col(1) == (QVector<double>{0, 1, 2, 3}));
    CHECK(unarySetOp(d, "inv") && std::isinf(d.col(1)[0]) && d.col(1)[1] == 1);
    d = mkSet("xydxdy", {{1}, {2}, {0.1}, {0.2}});
    CHECK(unarySetOp(d, "swapxy") && d.type() == "xydxdy" && d.col(0)[0] == 2 && d.col(2)[0] == 0.2);
    d = mkSet("xydy", {{1}, {2}, {0.3}});
    CHECK(unarySetOp(d, "swapxy") && d.type() == "xydx" && d.col(2)[0] == 0.3);
    d = mkSet("xydydy", {{1}, {2}, {0.1}, {0.3}});
    CHECK(unarySetOp(d, "neg") && d.col(2)[0] == 0.3 && d.col(3)[0] == 0.1);
    d = mkSet("bar", {{1}, {2}});
    CHECK(!unarySetOp(d, "swapxy", &err));
    d = mkSet("xy", {{0, 1}, {0, 0}});
    CHECK(!unarySetOp(d, "normmax", &err));

    // сумма и среднее нескольких наборов (среднее ± σ)
    const DataSet m1 = mkSet("xy", {{0, 1, 2}, {1, 1, 1}});
    const DataSet m2 = mkSet("xy", {{0, 1, 2}, {3, 3, 3}});
    const DataSet m3 = mkSet("xy", {{0, 2}, {5, 5}});  // интерполируется в x = 1
    CHECK(aggregateSets({&m1, &m2, &m3}, false, false, out, &inf) && out.col(1) == (QVector<double>{9, 9, 9}));
    CHECK(inf.interpolated);
    CHECK(aggregateSets({&m1, &m2, &m3}, true, true, out) && out.type() == "xydy");
    CHECK(out.col(1) == (QVector<double>{3, 3, 3}));
    NEAR(out.col(2)[1], 2.0, 1e-12);
    CHECK(!aggregateSets({&m1}, true, true, out, nullptr, &err));

    // высокоуровнево, с отменой через копию проекта
    Project pr = newProject();
    Graph &g = pr.graphs[0];
    QString e2;
    createFromFormula(g, 0, 10, 11, "x", &e2);
    createFromFormula(g, 0, 10, 21, "2*x", &e2);  // другая сетка
    g.sets[0].p.set("legend", "A");
    SetMathSpec sp;
    sp.op = "add";
    sp.ga = 0;
    sp.sa = 0;
    sp.gb = 0;
    sp.sb = 1;
    SetMathResult r = runSetMath(pr, sp);
    CHECK(r.ok && r.sets.size() == 1 && pr.graphs[0].sets.size() == 3);
    const DataSet &sum = *pr.graphs[0].set(r.sets[0].second);
    CHECK(sum.size() == 11 && sum.col(1)[10] == 30 && sum.p.s("legend") == "A + S1");
    CHECK(sum.p.i("line_color") != pr.graphs[0].sets[0].p.i("line_color"));  // новый цвет
    // умножить все выделенные на число прямо в наборах
    sp = SetMathSpec();
    sp.op = "mul";
    sp.withSet = false;
    sp.number = 3;
    sp.replace = true;
    sp.many = {qMakePair(0, 0), qMakePair(0, 1)};
    r = runSetMath(pr, sp);
    CHECK(r.ok && r.sets.size() == 2 && pr.graphs[0].sets.size() == 3);
    CHECK(pr.graphs[0].sets[0].col(1)[10] == 30 && pr.graphs[0].sets[1].col(1)[20] == 60);
    CHECK(pr.graphs[0].p.v4("world")[3] >= 60);  // масштаб расширился
    CHECK(setMathLabel(pr, sp, 0, 0) == "A * 3");
    sp.op = "add";
    sp.number = -2;
    CHECK(setMathLabel(pr, sp, 0, 1) == "S1 - 2");
    // среднее — новым набором
    sp = SetMathSpec();
    sp.op = "mean";
    sp.many = {qMakePair(0, 0), qMakePair(0, 1)};
    r = runSetMath(pr, sp);
    CHECK(r.ok && pr.graphs[0].sets.last().type() == "xydy" && r.message.contains("11"));
    // ошибка — проект не меняется
    sp = SetMathSpec();
    sp.op = "normmax";
    sp.sa = 99;
    const int before = pr.graphs[0].sets.size();
    CHECK(!runSetMath(pr, sp).ok && pr.graphs[0].sets.size() == before);
    CHECK(!parseAgr(writeAgr(pr)).graphs.isEmpty());
}

static void testSetExpression()
{
    CHECK(normalizeSetExpression("y = s1 + S2 − g1.s0 × 2") == "s1.y + S2.y - g1.s0.y * 2");
    CHECK(normalizeSetExpression("sin(s1.x) + s10") == "sin(s1.x) + s10.y");

    Project pr = newProject();
    Graph &g = pr.graphs[0];
    QString err;
    createFromFormula(g, 0, 10, 11, "x", &err);        // s0
    createFromFormula(g, 0, 10, 11, "2*x", &err);      // s1
    createFromFormula(g, 0, 10, 11, "1", &err);        // s2
    createFromFormula(g, 2, 6, 5, "100", &err);        // s3 — другая, более узкая сетка
    createFromFormula(g, 0, 10, 3, "10*x", &err);      // s4 — редкая сетка: 0, 5, 10
    Graph g1(1);  // после append ссылка g недействительна
    pr.graphs.append(g1);
    createFromFormula(pr.graphs[1], 0, 10, 11, "-x", &err);  // g1.s0

    DataSet out;
    CombineInfo inf;
    CHECK(setExpression(pr, 0, "s0.y + s1.y + s2.y", out, &inf));
    CHECK(out.size() == 11 && out.col(1)[10] == 31 && !inf.interpolated && inf.dropped == 0);
    CHECK(setExpression(pr, 0, "s1 - s0 - s2", out) && out.col(1)[4] == 3);
    CHECK(setExpression(pr, 0, "(s0 + s1) / 2", out) && out.col(1)[2] == 3);
    CHECK(setExpression(pr, 0, "s0.y − 0.5×s1.y + x", out) && out.col(1)[6] == 6);
    CHECK(setExpression(pr, 0, "s0 + g1.s0", out) && out.col(1)[7] == 0);
    // разные сетки: s3 есть только на 2…6
    CHECK(setExpression(pr, 0, "s0 + s3", out, &inf));
    CHECK(inf.interpolated && inf.dropped == 6 && out.size() == 5 && out.col(0)[0] == 2 && out.col(1)[0] == 102);
    // интерполяция редкого набора на частую сетку
    CHECK(setExpression(pr, 0, "s4", out, &inf, nullptr, 0, 0) && out.size() == 11 && out.col(1)[3] == 30);
    // сетка — заданного набора
    CHECK(setExpression(pr, 0, "s0 + s1", out, &inf, nullptr, 0, 4) && out.size() == 3 && out.col(1)[1] == 15);
    // ошибки
    CHECK(!setExpression(pr, 0, "s0 + s9", out, nullptr, &err) && err.contains("S9"));
    CHECK(!setExpression(pr, 0, "2 + 3", out, nullptr, &err));
    CHECK(!setExpression(pr, 0, "s0 +", out, nullptr, &err) && !err.isEmpty());
    CHECK(!setExpression(pr, 0, "", out, nullptr, &err));
    CHECK(!setExpression(pr, 0, "s0.y3", out, nullptr, &err));  // у xy нет y3
    // деление на ноль — точка пропускается
    CHECK(setExpression(pr, 0, "s2 / s0", out, &inf) && inf.nonFinite == 1 && out.size() == 10);

    CHECK((setExpressionFor({qMakePair(0, 1), qMakePair(0, 2), qMakePair(1, 0)}, 0, "sum") == "s1.y + s2.y + g1.s0.y"));
    CHECK((setExpressionFor({qMakePair(0, 1), qMakePair(0, 2)}, 0, "diff") == "s1.y - s2.y"));
    CHECK((setExpressionFor({qMakePair(0, 1), qMakePair(0, 2)}, 0, "mean") == "(s1.y + s2.y) / 2"));

    // через runSetMath: новый набор, подпись из имён наборов, отмена не нужна при ошибке
    pr.graphs[0].sets[0].p.set("legend", "A");
    SetMathSpec sp;
    sp.op = "expr";
    sp.ga = 0;
    sp.expr = "s0 + s1 - s2";
    const int before = pr.graphs[0].sets.size();
    SetMathResult r = runSetMath(pr, sp);
    CHECK(r.ok && pr.graphs[0].sets.size() == before + 1);
    const DataSet &res = pr.graphs[0].sets.last();
    CHECK(res.col(1)[10] == 29 && res.p.s("legend") == "A + S1 - S2" && res.p.s("comment") == "A + S1 - S2");
    sp.expr = "s0 + s42";
    CHECK(!runSetMath(pr, sp).ok && pr.graphs[0].sets.size() == before + 1);
}

static void testStyles()
{
    Project base = newProject();
    QString err;
    for (const char *f : {"x", "2*x", "3*x"})
        createFromFormula(base.graphs[0], 0, 10, 11, f, &err);
    base.graphs[0].p.setV4("world", 0, 0, 10, 30);
    base.graphs[0].axes[0].p.set("tick_major", 0.5);
    CHECK(styleDefs().size() >= 10);
    for (const StyleDef &sd : styleDefs()) {
        Project pr = base;
        applyStyle(pr, sd.key);
        Project back = parseAgr(writeAgr(pr));
        CHECK(back.graphs.size() == 1 && back.graphs[0].sets.size() == 3);
        CHECK(!renderImage(pr, 20).isNull());
    }
    Project pr = base;
    applyStyle(pr, "dark");
    CHECK(pr.bgColor > 15 && pr.color(pr.bgColor).lightness() < 60);
    CHECK(pr.color(pr.graphs[0].axes[0].p.i("ticklabel_color")).lightness() > 180);
    CHECK(pr.paletteName() == "bright");
    applyStyle(pr, "modern");  // обратно: белый фон, чёрные оси
    CHECK(pr.bgColor == 0 && pr.graphs[0].axes[0].p.i("ticklabel_color") == 1);

    pr = base;
    applyStyle(pr, "bw");
    QSet<int> styles;
    for (const DataSet &s : pr.graphs[0].sets) {
        CHECK(s.p.i("line_color") == 1);
        styles << s.p.i("line_linestyle");
    }
    CHECK(styles.size() == 3);
    applyStyle(pr, "modern");  // после ч/б — снова разные цвета и сплошные линии
    QSet<int> colors;
    for (const DataSet &s : pr.graphs[0].sets) {
        colors << s.p.i("line_color");
        CHECK(s.p.i("line_linestyle") == 1);
    }
    CHECK(colors.size() == 3 && !colors.contains(1));

    pr = base;
    applyStyle(pr, "grid");
    CHECK(pr.graphs[0].axes[0].p.b("tick_major_grid") && pr.graphs[0].axes[1].p.b("tick_major_grid"));
    applyStyle(pr, "paper");
    CHECK(!pr.graphs[0].axes[0].p.b("tick_major_grid"));

    pr = base;
    applyStyle(pr, "presentation");
    CHECK(pr.graphs[0].axes[0].p.d("tick_major") >= 10.0 / 6);
    CHECK(pr.graphs[0].axes[0].p.d("label_char_size") > 1.2);

    for (const PaletteDef &pd : paletteDefs()) {
        Project q = base;
        q.applyPalette(pd.colors);
        CHECK(pd.colors.size() == 16 && q.paletteName() == pd.key);
    }
    pr = base;
    gradientColors(pr, {qMakePair(0, 0), qMakePair(0, 1), qMakePair(0, 2)}, "viridis");
    colors.clear();
    for (const DataSet &s : pr.graphs[0].sets)
        colors << s.p.i("line_color");
    CHECK(colors.size() == 3);
    CHECK(pr.color(pr.graphs[0].sets[0].p.i("line_color")).lightness() <
          pr.color(pr.graphs[0].sets[2].p.i("line_color")).lightness());
}

static void testLayersAndFill()
{
    CHECK((layerOrder({0, 1, 2, 3}, {1}, LayerFront) == QVector<int>{0, 2, 3, 1}));
    CHECK((layerOrder({0, 1, 2, 3}, {1, 2}, LayerBack) == QVector<int>{1, 2, 0, 3}));
    CHECK((layerOrder({0, 1, 2, 3}, {1}, LayerForward) == QVector<int>{0, 2, 1, 3}));
    CHECK((layerOrder({0, 1, 2, 3}, {2}, LayerBackward) == QVector<int>{0, 2, 1, 3}));
    CHECK((layerOrder({0, 1, 2, 3}, {3}, LayerForward) == QVector<int>{0, 1, 2, 3}));

    Project pr = newProject();
    Graph &g = pr.graphs[0];
    QString err;
    for (const char *f : {"x", "2*x", "3*x"})
        createFromFormula(g, 0, 1, 5, f, &err);
    g.sets[2].p.set("legend", "third");
    const auto map = reorderSets(g, {2, 0, 1});
    CHECK(map.value(2) == 0 && map.value(0) == 1 && map.value(1) == 2);
    CHECK(g.sets[0].id == 0 && g.sets[0].p.s("legend") == "third");
    // после записи и чтения порядок сохраняется (Grace рисует по номерам)
    Project back = parseAgr(writeAgr(pr));
    CHECK(back.graphs[0].sets[0].p.s("legend") == "third");

    // перенос в другой график и перенумерация графиков
    Graph g2(1);
    pr.graphs.append(g2);
    DrawObject o("string");
    o.p.set("loctype", "world");
    o.graph = 1;
    pr.objects.append(o);
    const auto mm = moveSetsToGraph(pr, {qMakePair(0, 0)}, 1, -1);
    CHECK(pr.graphs[0].sets.size() == 2 && pr.graph(1)->sets.size() == 1);
    CHECK(mm.value(qMakePair(0, 0)) == 0);
    const auto gm = reorderGraphs(pr, {1, 0});
    CHECK(gm.value(1) == 0 && pr.graphs[0].sets.size() == 1);
    CHECK(pr.objects[0].graph == 0);

    // заливка под кривой
    Graph &gg = pr.graphs[1];
    fillToAxis(pr, gg.id, gg.sets[0].id, 1);
    const DataSet &d = pr.graphs[1].sets[0];
    CHECK(d.p.i("fill_type") == 2 && d.p.i("baseline_type") == 0 && d.p.i("fill_pattern") == 1);
    CHECK(fillToAxisMode(d) == 1);
    CHECK(pr.color(d.p.i("fill_color")).lightness() > pr.color(d.p.i("line_color")).lightness());
    fillToAxis(pr, gg.id, gg.sets[0].id, 2);
    CHECK(fillToAxisMode(pr.graphs[1].sets[0]) == 2);
    fillToAxis(pr, gg.id, gg.sets[0].id, 0);
    CHECK(fillToAxisMode(pr.graphs[1].sets[0]) == 0);
}

static void testTranslations()
{
    // все русские строки интерфейса есть в английском словаре
    setLanguage(Lang::En);
    CHECK(T("Открыть…") == "Open…");
    const QStringList missing = QStringList();  // заполняется проверкой исходников ниже
    setLanguage(Lang::Ru);
    CHECK(T("Открыть…") == QString::fromUtf8("Открыть…"));
}

static void testTables()
{
    auto t = readTables("# x y z\nx y z\n1 2 3\n4,5,6\n\n7 8 9\n&\n1 1\n", false);
    CHECK(t.size() == 2);
    CHECK(t[0].rows() == 3);
    CHECK(t[0].headers == QStringList({"x", "y", "z"}));
    auto t2 = readTables("1 2\n3 4\n\n5 6\n", true);
    CHECK(t2.size() == 2);
}

static void testRenderAll(const QString &outDir)
{
    QDir d(NG_TEST_DATA);
    const auto files = d.entryList({"*.agr"}, QDir::Files, QDir::Name);
    CHECK(files.size() > 10);
    for (const QString &f : files) {
        Project pr;
        QString err;
        const bool ok = loadProject(d.filePath(f), pr, &err);
        CHECK(ok);
        if (!ok) {
            std::fprintf(stderr, "  не открылся %s: %s\n", qPrintable(f), qPrintable(err));
            continue;
        }
        // двойная запись даёт тот же текст
        const QString a = writeAgr(pr);
        Project pr2 = parseAgr(a);
        const QString b = writeAgr(pr2);
        if (a != b)
            std::fprintf(stderr, "  нестабильная запись: %s\n", qPrintable(f));
        CHECK(a == b);
        int nsets = 0;
        for (auto &g : pr.graphs) nsets += g.sets.size();
        int nsets2 = 0;
        for (auto &g : pr2.graphs) nsets2 += g.sets.size();
        CHECK(nsets == nsets2);

        QImage img = renderImage(pr, 100);
        CHECK(!img.isNull());
        // на картинке что-то нарисовано
        int dark = 0;
        for (int y = 0; y < img.height(); y += 3)
            for (int x = 0; x < img.width(); x += 3)
                if (qGray(img.pixel(x, y)) < 200) ++dark;
        if (dark < 20)
            std::fprintf(stderr, "  пустая картинка: %s\n", qPrintable(f));
        CHECK(dark >= 20);
        if (!outDir.isEmpty())
            img.save(QDir(outDir).filePath(QFileInfo(f).completeBaseName() + ".png"));
    }
}

static void testExport(const QString &dir)
{
    Project pr = newProject();
    Graph &g = pr.graphs[0];
    QString err;
    createFromFormula(g, 0, 10, 100, "sin(x)", &err);
    autoscaleGraph(g);
    g.p.set("title", "Test \\xa\\f{} = x\\S2\\N");
    const QString base = dir.isEmpty() ? QDir::tempPath() : dir;
    for (const char *ext : {"png", "pdf", "svg", "jpg"}) {
        const QString path = QDir(base).filePath(QString("export_test.") + ext);
        CHECK(exportFile(pr, path, 150, false, &err));
        CHECK(QFileInfo(path).size() > 500);
    }
    // сохранение и открытие
    const QString agr = QDir(base).filePath("export_test.agr");
    CHECK(saveProject(pr, agr, &err));
    Project back;
    CHECK(loadProject(agr, back, &err));
    CHECK(back.graphs[0].sets.size() == 1);
    CHECK(back.graphs[0].sets[0].size() == 100);
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", qgetenv("QT_QPA_PLATFORM").isEmpty() ? "offscreen" : qgetenv("QT_QPA_PLATFORM"));
    QGuiApplication app(argc, argv);
    const QString outDir = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
    if (!outDir.isEmpty())
        QDir().mkpath(outDir);
    testProps();
    testParseMinimal();
    testXvg();
    testExpr();
    testFormat();
    testTicks();
    testOps();
    testTables();
    testLayersAndFill();
    testTextData();
    testSetMath();
    testSetExpression();
    testStyles();
    testTranslations();
    testRenderAll(outDir);
    testExport(outDir);
    std::printf("%d проверок пройдено, %d с ошибкой\n", g_ok, g_fail);
    return g_fail == 0 ? 0 : 1;
}
