#include "setmath.h"
#include "i18n.h"
#include "expr.h"
#include "ops.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <QHash>
#include <QRegularExpression>

namespace ng {

QString mathOpSymbol(MathOp op)
{
    switch (op) {
    case MathOp::Add: return QStringLiteral("+");
    case MathOp::Sub: return QStringLiteral("−");
    case MathOp::Mul: return QStringLiteral("×");
    case MathOp::Div: return QStringLiteral("÷");
    case MathOp::Pow: return QStringLiteral("^");
    }
    return QString();
}

// В подписях наборов — только ASCII, чтобы файл читался xmgrace (latin-1)
static QString asciiSymbol(MathOp op)
{
    switch (op) {
    case MathOp::Add: return QStringLiteral("+");
    case MathOp::Sub: return QStringLiteral("-");
    case MathOp::Mul: return QStringLiteral("*");
    case MathOp::Div: return QStringLiteral("/");
    case MathOp::Pow: return QStringLiteral("^");
    }
    return QString();
}

static bool opFromName(const QString &n, MathOp &op)
{
    static const QHash<QString, MathOp> m = {
        {"add", MathOp::Add}, {"sub", MathOp::Sub}, {"mul", MathOp::Mul}, {"div", MathOp::Div}, {"pow", MathOp::Pow}};
    auto it = m.find(n);
    if (it == m.end())
        return false;
    op = *it;
    return true;
}

static double apply(MathOp op, double a, double b)
{
    switch (op) {
    case MathOp::Add: return a + b;
    case MathOp::Sub: return a - b;
    case MathOp::Mul: return a * b;
    case MathOp::Div: return a / b;
    case MathOp::Pow: return std::pow(a, b);
    }
    return a;
}

QChar columnRole(const QString &t, int col)
{
    if (col == 0)
        return 'x';
    if (col == 1)
        return 'y';
    if (t == "xydx" || t == "xydxdx")
        return 'X';
    if (t == "xydy" || t == "xydydy" || t == "bardy" || t == "bardydy")
        return 'Y';
    if (t == "xydxdy" || t == "xyvmap")
        return col == 2 ? 'X' : 'Y';
    if (t == "xydxdxdydy")
        return col < 4 ? 'X' : 'Y';
    if (t == "xyhilo" || t == "xyboxplot")
        return 'y';
    return 'o';
}

// Погрешности по Y (не векторы xyvmap): номера столбцов
static QVector<int> errYCols(const QString &t)
{
    QVector<int> out;
    if (t == "xyvmap")
        return out;
    for (int k = 2; k < setTypeColumns(t); ++k)
        if (columnRole(t, k) == 'Y')
            out << k;
    return out;
}

static double errY(const DataSet &s, const QVector<int> &cols, int i)
{
    if (cols.isEmpty())
        return 0;
    double e = 0;
    for (int k : cols)
        e += std::fabs(s.col(k).value(i));
    return e / cols.size();  // асимметричные — среднее двух
}

bool mathWithNumber(DataSet &s, MathOp op, double c, bool onX, QString *error)
{
    if (!std::isfinite(c)) {
        if (error)
            *error = T("Число задано неверно");
        return false;
    }
    if (op == MathOp::Div && c == 0) {
        if (error)
            *error = T("Деление на ноль");
        return false;
    }
    const QString t = s.type();
    const QChar mainRole = onX ? 'x' : 'y', errRole = onX ? 'X' : 'Y';
    const QVector<double> base = s.col(onX ? 0 : 1);
    for (int k = 0; k < s.cols.size(); ++k) {
        const QChar r = columnRole(t, k);
        QVector<double> &v = s.cols[k];
        if (r == mainRole) {
            for (double &x : v)
                x = apply(op, x, c);
        } else if (r == errRole) {
            if (op == MathOp::Mul || op == MathOp::Div) {
                const double f = op == MathOp::Mul ? std::fabs(c) : 1.0 / std::fabs(c);
                for (double &x : v)
                    x *= f;
            } else if (op == MathOp::Pow && t != "xyvmap") {
                for (int i = 0; i < v.size(); ++i)
                    v[i] *= std::fabs(c * std::pow(base.value(i), c - 1));
            }
        }
    }
    return true;
}

// Значения набора на чужой сетке X: сама сетка совпадает — по номеру точки,
// иначе линейная интерполяция по упорядоченным точкам.
namespace {
struct Sampler {
    bool same = false;
    bool hasErr = false;
    QVector<double> xs, ys, es;
    double eps = 0;

    bool init(const DataSet &b, const QVector<double> &grid, QString *error)
    {
        const QVector<int> ec = errYCols(b.type());
        hasErr = !ec.isEmpty();
        const auto &bx = b.col(0), &by = b.col(1);
        const int n = b.size();
        same = n == grid.size();
        for (int i = 0; same && i < n; ++i)
            if (!(std::fabs(bx[i] - grid[i]) <= 1e-9 * qMax(1.0, std::fabs(grid[i]))))
                same = false;
        QVector<int> idx;
        for (int i = 0; i < n; ++i)
            if (same || (std::isfinite(bx[i]) && std::isfinite(by[i])))
                idx << i;
        if (idx.isEmpty()) {
            if (error)
                *error = T("Во втором наборе нет точек");
            return false;
        }
        if (!same)
            std::stable_sort(idx.begin(), idx.end(), [&](int p, int q) { return bx[p] < bx[q]; });
        for (int i : idx) {
            xs << bx[i];
            ys << by[i];
            es << errY(b, ec, i);
        }
        if (!same)
            eps = 1e-9 * qMax(std::fabs(xs.last() - xs.first()), qMax(std::fabs(xs.first()), std::fabs(xs.last())));
        return true;
    }

    bool at(int i, double x, double &y, double &e) const
    {
        if (same) {
            y = ys[i];
            e = es[i];
            return true;
        }
        const int n = xs.size();
        const int j = int(std::lower_bound(xs.begin(), xs.end(), x) - xs.begin());
        if (j < n && std::fabs(xs[j] - x) <= eps) {
            y = ys[j];
            e = es[j];
            return true;
        }
        if (j > 0 && std::fabs(xs[j - 1] - x) <= eps) {
            y = ys[j - 1];
            e = es[j - 1];
            return true;
        }
        if (j == 0 || j == n)
            return false;
        const double t = (x - xs[j - 1]) / (xs[j] - xs[j - 1]);
        y = ys[j - 1] + t * (ys[j] - ys[j - 1]);
        e = es[j - 1] + t * (es[j] - es[j - 1]);
        return true;
    }
};
}  // namespace

static double propagate(MathOp op, double a, double da, double b, double db, double r)
{
    switch (op) {
    case MathOp::Add:
    case MathOp::Sub: return std::hypot(da, db);
    case MathOp::Mul: return std::hypot(b * da, a * db);
    case MathOp::Div: return std::hypot(da / b, a * db / (b * b));
    case MathOp::Pow: {
        const double t1 = da != 0 ? b * std::pow(a, b - 1) * da : 0.0;
        const double t2 = db != 0 ? r * std::log(a) * db : 0.0;
        return std::hypot(t1, t2);
    }
    }
    return 0;
}

static void storeResult(DataSet &out, const QString &like, bool err, QVector<double> x, QVector<double> y,
                        QVector<double> e)
{
    QString t = like.startsWith("bar") ? "bar" : "xy";
    if (err)
        t += "dy";
    out.setType(t);
    out.cols = {x, y};
    if (err)
        out.cols << e;
    out.strings.clear();
}

bool combineSets(const DataSet &a, const DataSet &b, MathOp op, DataSet &out, CombineInfo *info, QString *error)
{
    CombineInfo inf;
    if (a.size() == 0) {
        if (error)
            *error = T("Первый набор пуст");
        return false;
    }
    const auto &ax = a.col(0), &ay = a.col(1);
    Sampler sb;
    if (!sb.init(b, ax, error))
        return false;
    inf.interpolated = !sb.same;
    const QVector<int> ea = errYCols(a.type());
    const bool err = !ea.isEmpty() || sb.hasErr;
    QVector<double> ox, oy, oe;
    for (int i = 0; i < a.size(); ++i) {
        double yb = 0, eb = 0;
        if (!std::isfinite(ax[i]) || !sb.at(i, ax[i], yb, eb)) {
            ++inf.dropped;
            continue;
        }
        const double r = apply(op, ay[i], yb);
        if (!std::isfinite(r)) {
            ++inf.nonFinite;
            continue;
        }
        ox << ax[i];
        oy << r;
        if (err) {
            const double e = propagate(op, ay[i], errY(a, ea, i), yb, eb, r);
            oe << (std::isfinite(e) ? e : 0.0);
        }
    }
    inf.kept = ox.size();
    if (info)
        *info = inf;
    if (ox.isEmpty()) {
        if (error)
            *error = inf.dropped ? T("У наборов нет общего диапазона X") : T("Результат не определён ни в одной точке");
        return false;
    }
    storeResult(out, a.type(), err, ox, oy, oe);
    return true;
}

bool aggregateSets(const QVector<const DataSet *> &sets, bool mean, bool withStd, DataSet &out, CombineInfo *info,
                   QString *error)
{
    CombineInfo inf;
    if (sets.size() < 2) {
        if (error)
            *error = T("Выберите хотя бы два набора");
        return false;
    }
    const DataSet &a = *sets[0];
    if (a.size() == 0) {
        if (error)
            *error = T("Первый набор пуст");
        return false;
    }
    const auto &ax = a.col(0);
    QVector<Sampler> smp(sets.size());
    bool anyErr = false;
    for (int k = 0; k < sets.size(); ++k) {
        if (!smp[k].init(*sets[k], ax, error))
            return false;
        anyErr = anyErr || smp[k].hasErr;
        inf.interpolated = inf.interpolated || !smp[k].same;
    }
    const bool stdErr = mean && withStd;
    const bool err = stdErr || anyErr;
    const int n = sets.size();
    QVector<double> ox, oy, oe, v(n), e(n);
    for (int i = 0; i < a.size(); ++i) {
        bool ok = std::isfinite(ax[i]);
        for (int k = 0; ok && k < n; ++k)
            ok = smp[k].at(i, ax[i], v[k], e[k]) && std::isfinite(v[k]);
        if (!ok) {
            ++inf.dropped;
            continue;
        }
        const double sum = std::accumulate(v.begin(), v.end(), 0.0);
        const double r = mean ? sum / n : sum;
        double er = 0;
        if (stdErr) {
            double s2 = 0;
            for (double q : v)
                s2 += (q - r) * (q - r);
            er = std::sqrt(s2 / (n - 1));
        } else if (anyErr) {
            double s2 = 0;
            for (double q : e)
                s2 += q * q;
            er = std::sqrt(s2) / (mean ? n : 1);
        }
        ox << ax[i];
        oy << r;
        oe << er;
    }
    inf.kept = ox.size();
    if (info)
        *info = inf;
    if (ox.isEmpty()) {
        if (error)
            *error = T("У наборов нет общего диапазона X");
        return false;
    }
    storeResult(out, a.type(), err, ox, oy, oe);
    return true;
}

// ---------------------------------------------------------------------------
// Формула из наборов

QString normalizeSetExpression(const QString &text)
{
    QString t = text.trimmed();
    t.replace(QChar(0x2212), '-').replace(QChar(0x2013), '-').replace(QChar(0xD7), '*').replace(QChar(0x22C5), '*')
        .replace(QChar(0xB7), '*').replace(QChar(0xF7), '/');
    static const QRegularExpression lhs(QStringLiteral("^y\\s*=(?!=)\\s*"), QRegularExpression::CaseInsensitiveOption);
    t.remove(lhs);
    // «s1» без столбца — это «s1.y»
    static const QRegularExpression bare(QStringLiteral("(?<![\\w.$])((?:[gG]\\d+\\.)?[sS]\\d+)(?![\\w.])"));
    t.replace(bare, QStringLiteral("\\1.y"));
    return t.trimmed();
}

// «g1.s2.y» / «s2.y» → график, набор, номер столбца
static bool parseRef(const QString &name, int home, int &g, int &s, int &col)
{
    static const QRegularExpression re(QStringLiteral("^(?:g(\\d+)\\.)?s(\\d+)\\.(x|y|y1|y2|y3|y4|z)$"),
                                       QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(name);
    if (!m.hasMatch())
        return false;
    g = m.captured(1).isEmpty() ? home : m.captured(1).toInt();
    s = m.captured(2).toInt();
    static const QStringList cols = {"x", "y", "y1", "y2", "y3", "y4"};
    const QString c = m.captured(3).toLower();
    col = c == "z" ? 2 : cols.indexOf(c);
    return true;
}

static bool sameGrid(const QVector<double> &bx, const QVector<double> &grid)
{
    if (bx.size() != grid.size())
        return false;
    for (int i = 0; i < bx.size(); ++i)
        if (!(std::fabs(bx[i] - grid[i]) <= 1e-9 * qMax(1.0, std::fabs(grid[i]))))
            return false;
    return true;
}

// Столбец col набора b в точках grid; ok[i] = 0, где значения нет
static void resampleColumn(const DataSet &b, int col, const QVector<double> &grid, QVector<double> &out,
                           QVector<char> &ok, bool &interpolated)
{
    const auto &bx = b.col(0), &bv = b.col(col);
    const int n = grid.size();
    out = QVector<double>(n, qQNaN());
    if (sameGrid(bx, grid)) {
        for (int i = 0; i < n; ++i) {
            out[i] = bv.value(i, qQNaN());
            if (!std::isfinite(out[i]))
                ok[i] = 0;
        }
        return;
    }
    interpolated = true;
    QVector<int> idx;
    for (int i = 0; i < b.size(); ++i)
        if (std::isfinite(bx[i]) && std::isfinite(bv.value(i, qQNaN())))
            idx << i;
    std::stable_sort(idx.begin(), idx.end(), [&](int p, int q) { return bx[p] < bx[q]; });
    QVector<double> xs, vs;
    for (int i : idx) {
        xs << bx[i];
        vs << bv[i];
    }
    const int m = xs.size();
    if (m == 0) {
        ok.fill(0);
        return;
    }
    const double eps = 1e-9 * qMax(std::fabs(xs.last() - xs.first()), qMax(std::fabs(xs.first()), std::fabs(xs.last())));
    for (int i = 0; i < n; ++i) {
        const double x = grid[i];
        const int j = int(std::lower_bound(xs.begin(), xs.end(), x) - xs.begin());
        if (j < m && std::fabs(xs[j] - x) <= eps)
            out[i] = vs[j];
        else if (j > 0 && std::fabs(xs[j - 1] - x) <= eps)
            out[i] = vs[j - 1];
        else if (j == 0 || j == m)
            ok[i] = 0;
        else
            out[i] = vs[j - 1] + (x - xs[j - 1]) / (xs[j] - xs[j - 1]) * (vs[j] - vs[j - 1]);
    }
}

bool setExpression(const Project &pr, int home, const QString &text, DataSet &out, CombineInfo *info,
                   QString *error, int gridG, int gridS, QVector<QPair<int, int>> *refsOut)
{
    auto fail = [&](const QString &m) {
        if (error)
            *error = m;
        return false;
    };
    const QString norm = normalizeSetExpression(text);
    if (norm.isEmpty())
        return fail(T("Введите формулу, например s0.y + s1.y"));
    Expr e;
    QString perr;
    if (!e.parse(norm, &perr))
        return fail(perr);
    struct Ref {
        QString name;
        const DataSet *set;
        int col;
    };
    QVector<Ref> refs;
    QVector<QPair<int, int>> sets;
    for (const QString &v : e.variables()) {
        int g, s, col;
        if (!parseRef(v, home, g, s, col))
            continue;
        const Graph *gr = pr.graph(g);
        const DataSet *d = gr ? gr->set(s) : nullptr;
        const QString shown = v.section('.', 0, -2).toUpper();
        if (!d)
            return fail(T("Нет набора %1").arg(shown));
        if (col < 0 || col >= d->cols.size())
            return fail(T("У набора %1 нет столбца %2").arg(shown, v.section('.', -1)));
        refs << Ref{v, d, col};
        if (!sets.contains(qMakePair(g, s)))
            sets << qMakePair(g, s);
    }
    if (refsOut)
        *refsOut = sets;
    const DataSet *grid = nullptr;
    if (gridS >= 0)
        if (const Graph *gr = pr.graph(gridG < 0 ? home : gridG))
            grid = gr->set(gridS);
    if (!grid && !refs.isEmpty())
        grid = refs[0].set;
    if (!grid)
        return fail(T("В формуле нет ни одного набора — например, s0.y + s1.y"));
    if (grid->size() == 0)
        return fail(T("Набор пуст"));

    const QVector<double> gx = grid->col(0);
    const int n = gx.size();
    QVector<char> ok(n, 1);
    for (int i = 0; i < n; ++i)
        if (!std::isfinite(gx[i]))
            ok[i] = 0;
    CombineInfo inf;
    QHash<QString, QVector<double>> cols;
    for (const Ref &r : refs) {
        if (cols.contains(r.name))
            continue;
        resampleColumn(*r.set, r.col, gx, cols[r.name], ok, inf.interpolated);
    }
    // x, y, y1… в формуле — столбцы набора-сетки
    QVector<QVector<double>> own = grid->cols;
    own[0] = gx;
    QVector<const QVector<double> *> ptrs;
    for (const auto &c : own)
        ptrs << &c;
    Expr::Ctx c;
    c.cols = &ptrs;
    c.n = n;
    c.resolver = [&cols](const QString &name) -> const QVector<double> * {
        auto it = cols.constFind(name);
        return it == cols.constEnd() ? nullptr : &*it;
    };
    QVector<double> ox, oy;
    for (int i = 0; i < n; ++i) {
        if (!ok[i]) {
            ++inf.dropped;
            continue;
        }
        c.row = i;
        const double r = e.eval(c);
        if (!std::isfinite(r)) {
            ++inf.nonFinite;
            continue;
        }
        ox << gx[i];
        oy << r;
    }
    inf.kept = ox.size();
    if (info)
        *info = inf;
    if (ox.isEmpty())
        return fail(inf.dropped ? T("У наборов нет общего диапазона X") : T("Результат не определён ни в одной точке"));
    storeResult(out, grid->type(), false, ox, oy, {});
    return true;
}

QString setExpressionFor(const QVector<QPair<int, int>> &sets, int home, const QString &kind)
{
    QStringList parts;
    for (const auto &p : sets)
        parts << (p.first == home ? QString("s%1.y").arg(p.second) : QString("g%1.s%2.y").arg(p.first).arg(p.second));
    if (parts.isEmpty())
        return QString();
    if (kind == "diff")
        return parts.join(" - ");
    if (kind == "mean")
        return parts.size() > 1 ? QString("(%1) / %2").arg(parts.join(" + ")).arg(parts.size()) : parts[0];
    return parts.join(" + ");
}

const QStringList &unarySetOps()
{
    static const QStringList ops = {"neg", "abs", "inv", "normmax", "normarea", "submin", "swapxy"};
    return ops;
}

bool unarySetOp(DataSet &s, const QString &op, QString *error)
{
    const QString t = s.type();
    auto fail = [&](const QString &m) {
        if (error)
            *error = m;
        return false;
    };
    if (s.size() == 0)
        return fail(T("Набор пуст"));
    auto forY = [&](const std::function<double(double)> &f) {
        for (int k = 0; k < s.cols.size(); ++k)
            if (columnRole(t, k) == 'y')
                for (double &x : s.cols[k])
                    x = f(x);
    };
    auto scaleAll = [&](double f) {  // значения и погрешности по Y
        for (int k = 0; k < s.cols.size(); ++k) {
            const QChar r = columnRole(t, k);
            if (r == 'y' || r == 'Y')
                for (double &x : s.cols[k])
                    x *= (r == 'y' ? f : std::fabs(f));
        }
    };
    if (op == "neg") {
        forY([](double v) { return -v; });
        const QVector<int> ec = errYCols(t);
        if (ec.size() == 2)  // верхняя и нижняя погрешности меняются местами
            std::swap(s.cols[ec[0]], s.cols[ec[1]]);
        if (t == "xyvmap")
            for (double &x : s.cols[3])
                x = -x;
        return true;
    }
    if (op == "abs") {
        forY([](double v) { return std::fabs(v); });
        return true;
    }
    if (op == "inv") {
        const QVector<double> y = s.col(1);
        forY([](double v) { return 1.0 / v; });
        for (int k : errYCols(t))
            for (int i = 0; i < s.size(); ++i)
                s.cols[k][i] = s.cols[k][i] / (y[i] * y[i]);
        return true;
    }
    if (op == "normmax") {
        double m = 0;
        for (double v : s.col(1))
            if (std::isfinite(v))
                m = qMax(m, std::fabs(v));
        if (m == 0)
            return fail(T("Все значения Y равны нулю"));
        scaleAll(1.0 / m);
        return true;
    }
    if (op == "normarea") {
        const auto &x = s.col(0), &y = s.col(1);
        QVector<int> idx;
        for (int i = 0; i < s.size(); ++i)
            if (std::isfinite(x[i]) && std::isfinite(y[i]))
                idx << i;
        std::stable_sort(idx.begin(), idx.end(), [&](int p, int q) { return x[p] < x[q]; });
        double area = 0;
        for (int k = 1; k < idx.size(); ++k)
            area += 0.5 * (y[idx[k]] + y[idx[k - 1]]) * (x[idx[k]] - x[idx[k - 1]]);
        if (!(std::fabs(area) > 0))
            return fail(T("Площадь под кривой равна нулю"));
        scaleAll(1.0 / std::fabs(area));
        return true;
    }
    if (op == "submin") {
        double m = qInf();
        for (double v : s.col(1))
            if (std::isfinite(v))
                m = qMin(m, v);
        if (!std::isfinite(m))
            return fail(T("Набор пуст"));
        forY([m](double v) { return v - m; });
        return true;
    }
    if (op == "swapxy") {
        static const QHash<QString, QString> sw = {
            {"xy", "xy"},         {"xydx", "xydy"},     {"xydy", "xydx"},           {"xydxdx", "xydydy"},
            {"xydydy", "xydxdx"}, {"xydxdy", "xydxdy"}, {"xydxdxdydy", "xydxdxdydy"}, {"xystring", "xystring"}};
        if (!sw.contains(t))
            return fail(T("Для наборов типа %1 недоступно").arg(t));
        QVector<QVector<double>> c = s.cols;
        std::swap(c[0], c[1]);
        if (t == "xydxdy")
            std::swap(c[2], c[3]);
        if (t == "xydxdxdydy") {
            std::swap(c[2], c[4]);
            std::swap(c[3], c[5]);
        }
        const QStringList strings = s.strings;
        s.setType(sw.value(t));
        s.cols = c;
        s.strings = strings;
        return true;
    }
    return fail(T("Неизвестная операция %1").arg(op));
}

// ---------------------------------------------------------------------------

bool isBinaryMathOp(const QString &op)
{
    MathOp m;
    return opFromName(op, m);
}

bool isAggregateOp(const QString &op) { return op == "sum" || op == "mean"; }

static QString setName(const Project &pr, int g, int s, int homeGraph)
{
    const Graph *gr = pr.graph(g);
    const DataSet *d = gr ? gr->set(s) : nullptr;
    QString n = d ? d->p.s("legend") : QString();
    if (n.isEmpty())
        n = QString("S%1").arg(s);
    if (g != homeGraph)
        n = QString("G%1.").arg(g) + n;
    return n;
}

static QString numberText(double c) { return QString::number(c, 'g', 10); }

QString setMathLabel(const Project &pr, const SetMathSpec &spec, int ga, int sa)
{
    if (spec.op == "expr") {
        // «s0.y + s1.y» → «sin(t) + S1» (подписи наборов вместо ссылок)
        QString t = normalizeSetExpression(spec.expr);
        static const QRegularExpression re(QStringLiteral("(?<![\\w.$])(?:[gG](\\d+)\\.)?[sS](\\d+)\\.[yY](?![\\w.])"));
        QString out;
        int last = 0;
        auto it = re.globalMatch(t);
        while (it.hasNext()) {
            const auto m = it.next();
            out += t.mid(last, m.capturedStart() - last);
            const int g = m.captured(1).isEmpty() ? ga : m.captured(1).toInt();
            out += setName(pr, g, m.captured(2).toInt(), ga);
            last = m.capturedEnd();
        }
        return out + t.mid(last);
    }
    const QString a = setName(pr, ga, sa, ga);
    MathOp op;
    if (opFromName(spec.op, op)) {
        if (spec.withSet)
            return QString("%1 %2 %3").arg(a, asciiSymbol(op), setName(pr, spec.gb, spec.sb, ga));
        double c = spec.number;
        if (op == MathOp::Add && c < 0) {
            op = MathOp::Sub;
            c = -c;
        } else if (op == MathOp::Sub && c < 0) {
            op = MathOp::Add;
            c = -c;
        }
        const QString e = QString("%1 %2").arg(asciiSymbol(op), numberText(c));
        return spec.onX ? QString("%1 (x %2)").arg(a, e) : QString("%1 %2").arg(a, e);
    }
    if (isAggregateOp(spec.op)) {
        QStringList names;
        for (const auto &p : spec.many)
            names << setName(pr, p.first, p.second, spec.many.value(0).first);
        return (spec.op == "sum" ? T("Сумма %1") : T("Среднее %1")).arg(names.join(", "));
    }
    if (spec.op == "neg") return "-" + a;
    if (spec.op == "abs") return "|" + a + "|";
    if (spec.op == "inv") return "1/" + a;
    if (spec.op == "normmax") return a + " / max";
    if (spec.op == "normarea") return T("%1 / площадь").arg(a);
    if (spec.op == "submin") return a + " - min";
    if (spec.op == "swapxy") return T("%1 (X и Y)").arg(a);
    return a;
}

SetMathResult runSetMath(Project &pr, const SetMathSpec &spec)
{
    SetMathResult res;
    auto get = [&](int g, int s) -> const DataSet * {
        const Graph *gr = pr.graph(g);
        return gr ? gr->set(s) : nullptr;
    };
    auto legendOf = [&](int g, int s) { const DataSet *d = get(g, s); return d ? d->p.s("legend") : QString(); };

    // записать результат: новый набор в графике g или замена набора s
    auto store = [&](int g, int s, const DataSet &data, const QString &label, bool withLegend) {
        Graph &gr = *pr.graph(g);
        int id = s;
        if (spec.replace && s >= 0) {  // s < 0 — всегда новый набор
            DataSet &d = *gr.set(s);
            d.setType(data.type());
            d.cols = data.cols;
            d.strings = data.strings;
        } else {
            DataSet &n = gr.newSet(data.type());
            n.cols = data.cols;
            n.strings = data.strings;
            n.p.set("comment", label);
            if (withLegend)
                n.p.set("legend", label);
            if (n.size() == 1) {  // одна точка видна только символом
                n.p.set("symbol", 1);
                n.p.set("symbol_fill_pattern", 1);
            }
            id = n.id;
        }
        ensureVisible(gr, {id});
        res.sets << qMakePair(g, id);
    };
    QStringList notes;
    auto noteInfo = [&](const CombineInfo &inf) {
        if (inf.interpolated)
            notes << T("значения интерполированы на сетку X первого набора");
        if (inf.dropped)
            notes << T("%1 точек вне общего диапазона X пропущено").arg(inf.dropped);
        if (inf.nonFinite)
            notes << T("%1 точек без значения (деление на ноль и т.п.) пропущено").arg(inf.nonFinite);
    };

    MathOp op = MathOp::Add;
    const bool binary = opFromName(spec.op, op);
    if (spec.op == "expr") {
        if (!pr.graph(spec.ga)) {
            res.message = T("Нет графика G%1").arg(spec.ga);
            return res;
        }
        DataSet out;
        CombineInfo inf;
        QString err;
        QVector<QPair<int, int>> refs;
        if (!setExpression(pr, spec.ga, spec.expr, out, &inf, &err, spec.gridG, spec.gridS, &refs)) {
            res.message = err;
            return res;
        }
        bool leg = false;
        for (const auto &r : refs)
            leg = leg || !legendOf(r.first, r.second).isEmpty();
        store(spec.ga, -1, out, setMathLabel(pr, spec, spec.ga, -1), leg);
        noteInfo(inf);
        res.ok = true;
        res.message = T("Точек: %1").arg(inf.kept);
    } else if (binary && spec.withSet) {
        const DataSet *a = get(spec.ga, spec.sa), *b = get(spec.gb, spec.sb);
        if (!a || !b) {
            res.message = T("Выберите оба набора");
            return res;
        }
        DataSet out = *a;
        CombineInfo inf;
        QString err;
        if (!combineSets(*a, *b, op, out, &inf, &err)) {
            res.message = err;
            return res;
        }
        const bool leg = !legendOf(spec.ga, spec.sa).isEmpty() || !legendOf(spec.gb, spec.sb).isEmpty();
        store(spec.ga, spec.sa, out, setMathLabel(pr, spec, spec.ga, spec.sa), leg);
        noteInfo(inf);
        res.ok = true;
        res.message = T("Точек: %1").arg(inf.kept);
    } else if (isAggregateOp(spec.op)) {
        QVector<const DataSet *> sets;
        bool leg = false;
        for (const auto &p : spec.many) {
            if (const DataSet *d = get(p.first, p.second)) {
                sets << d;
                leg = leg || !d->p.s("legend").isEmpty();
            }
        }
        DataSet out = sets.isEmpty() ? DataSet() : *sets[0];
        CombineInfo inf;
        QString err;
        if (!aggregateSets(sets, spec.op == "mean", spec.withStd, out, &inf, &err)) {
            res.message = err;
            return res;
        }
        const int g = spec.many[0].first;
        // сумма и среднее — всегда новый набор
        store(g, -1, out, setMathLabel(pr, spec, g, spec.many[0].second), leg);
        noteInfo(inf);
        res.ok = true;
        res.message = T("Точек: %1").arg(inf.kept);
    } else {
        // число или действие над одним набором — для A или всех выделенных
        const bool unary = unarySetOps().contains(spec.op);
        if (!binary && !unary) {
            res.message = T("Неизвестная операция %1").arg(spec.op);
            return res;
        }
        QVector<QPair<int, int>> targets = spec.many;
        if (targets.isEmpty())
            targets << qMakePair(spec.ga, spec.sa);
        QStringList errors;
        for (const auto &p : targets) {
            const DataSet *src = get(p.first, p.second);
            if (!src)
                continue;
            DataSet out = *src;
            QString err;
            const bool ok = unary ? unarySetOp(out, spec.op, &err) : mathWithNumber(out, op, spec.number, spec.onX, &err);
            if (!ok) {
                errors << QString("S%1: %2").arg(p.second).arg(err);
                continue;
            }
            store(p.first, p.second, out, setMathLabel(pr, spec, p.first, p.second), !src->p.s("legend").isEmpty());
        }
        res.ok = !res.sets.isEmpty();
        if (!errors.isEmpty())
            notes << errors;
        res.message = res.ok ? T("Наборов: %1").arg(res.sets.size()) : QString();
    }
    if (!notes.isEmpty())
        res.message += (res.message.isEmpty() ? QString() : QStringLiteral("; ")) + notes.join("; ");
    return res;
}

}  // namespace ng
