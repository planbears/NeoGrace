#include "ops.h"
#include "i18n.h"

#include "axes.h"
#include "expr.h"
#include "gracetext.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <numeric>
#include <QRegularExpression>

namespace ng {


// ---------------------------------------------------------------------------
// Оформление

const QVector<StyleDef> &styleDefs()
{
    static const QVector<StyleDef> defs = {
        {"modern", "Современный", "Helvetica, мягкие цвета, открытая рамка, деления наружу"},
        {"paper", "Для статьи", "Times, деления внутрь с четырёх сторон"},
        {"classic", "Классический xmgrace", "Как в xmgrace по умолчанию"},
        {"journal", "Журнальный", "Helvetica, тонкие линии и мелкий шрифт — как в Nature и Science"},
        {"presentation", "Для презентации", "Крупный шрифт и толстые линии — читается с проектора"},
        {"dark", "Тёмный", "Тёмный фон, светлые оси и яркие цвета — для экрана и слайдов"},
        {"bw", "Чёрно-белый", "Все кривые чёрные и различаются стилем линий и символами — для печати"},
        {"grid", "С сеткой", "Сетка по основным делениям, рамка со всех сторон"},
        {"minimal", "Минимализм", "Только левая и нижняя оси, без мелких делений, серые оси"},
        {"soft", "Мягкий фон", "Серо-голубое поле с белой сеткой, без рамки"},
    };
    return defs;
}

namespace {
struct StyleSpec {
    int font = 0;
    double lw = 1.0;          // рамка, оси, деления
    double setLw = -1;        // линии наборов (−1 — не трогать)
    QString inout = "in", place = "both";
    int frame = 0;            // 0 — закрытая рамка, 1 — только левая и нижняя оси
    bool framePattern = true; // рисовать рамку
    bool legendBox = true;
    double tickMajor = 1.0, tickMinor = 0.5;
    int minorTicks = -1;      // −1 — не менять (0 → 1)
    double labelSize = 1.0, ticklabelSize = 1.0, titleSize = 1.5, subtitleSize = 1.0, legendSize = 1.0;
    double symbolScale = 1.0;
    int maxTicks = 0;         // > 0 — не больше стольких основных делений
    QString palette;          // пусто — не менять
    QColor bg = Qt::white, axis = Qt::black, text = Qt::black;
    QColor frameBg;           // невалидный — без заливки поля
    bool grid = false;
    QColor gridColor;         // невалидный — цвет осей
    int gridStyle = 1;
    double gridLw = -1;
    bool fillSymbols = false;
    bool bw = false;
};

StyleSpec styleSpec(const QString &name)
{
    StyleSpec s;
    if (name == "modern") {
        s.font = 4; s.lw = 1.2; s.setLw = 2.0; s.inout = "out"; s.place = "normal"; s.frame = 1;
        s.legendBox = false; s.tickMajor = 0.7; s.tickMinor = 0.4; s.palette = "modern"; s.fillSymbols = true;
    } else if (name == "paper") {
        s.lw = 1.2; s.setLw = 1.5; s.legendBox = false;
    } else if (name == "journal") {
        s.font = 4; s.lw = 0.8; s.setLw = 1.0; s.inout = "out"; s.place = "normal"; s.frame = 1;
        s.legendBox = false; s.tickMajor = 0.6; s.tickMinor = 0.35; s.labelSize = 0.9; s.ticklabelSize = 0.85;
        s.titleSize = 1.1; s.subtitleSize = 0.9; s.legendSize = 0.85; s.symbolScale = 0.7; s.fillSymbols = true;
    } else if (name == "presentation") {
        s.font = 4; s.lw = 2.0; s.setLw = 3.0; s.inout = "out"; s.place = "normal"; s.frame = 1;
        s.legendBox = false; s.tickMajor = 1.0; s.tickMinor = 0.6; s.labelSize = 1.5; s.ticklabelSize = 1.35;
        s.titleSize = 1.8; s.subtitleSize = 1.3; s.legendSize = 1.3; s.symbolScale = 1.3; s.fillSymbols = true;
        s.palette = "modern"; s.maxTicks = 6;
    } else if (name == "dark") {
        s.font = 4; s.lw = 1.2; s.setLw = 2.0; s.inout = "out"; s.place = "normal"; s.frame = 1;
        s.legendBox = false; s.tickMajor = 0.7; s.tickMinor = 0.4; s.palette = "bright"; s.fillSymbols = true;
        s.bg = QColor(28, 29, 34); s.axis = QColor(200, 202, 208); s.text = QColor(230, 231, 235);
    } else if (name == "bw") {
        s.lw = 1.0; s.setLw = 1.5; s.bw = true;
    } else if (name == "grid") {
        s.font = 4; s.lw = 1.0; s.setLw = 2.0; s.legendBox = true; s.palette = "modern"; s.fillSymbols = true;
        s.tickMajor = 0.7; s.tickMinor = 0.4; s.grid = true; s.gridColor = QColor(200, 200, 200); s.gridStyle = 1;
        s.gridLw = 0.5;
    } else if (name == "minimal") {
        s.font = 4; s.lw = 1.0; s.setLw = 2.0; s.inout = "out"; s.place = "normal"; s.frame = 1;
        s.legendBox = false; s.tickMajor = 0.6; s.tickMinor = 0.3; s.minorTicks = 0; s.palette = "modern";
        s.fillSymbols = true; s.axis = QColor(110, 110, 110); s.text = QColor(60, 60, 60);
    } else if (name == "soft") {
        s.font = 4; s.lw = 1.0; s.setLw = 2.0; s.inout = "out"; s.place = "normal"; s.framePattern = false;
        s.legendBox = false; s.tickMajor = 0.0; s.tickMinor = 0.0; s.minorTicks = 0; s.palette = "modern";
        s.fillSymbols = true; s.frameBg = QColor(234, 234, 242); s.grid = true; s.gridColor = Qt::white;
        s.gridLw = 1.5; s.axis = QColor(234, 234, 242); s.text = QColor(50, 50, 50);
    } else {  // classic
        s.setLw = 1.0; s.palette = "classic";
    }
    return s;
}
}  // namespace

void applyStyle(Project &pr, const QString &name)
{
    const StyleSpec st = styleSpec(name);
    if (!st.palette.isEmpty())
        if (const auto *pal = paletteColors(st.palette))
            pr.applyPalette(*pal);
    auto idx = [&](const QColor &c, const char *n) {
        if (c == Qt::white)
            return 0;
        if (c == Qt::black)
            return 1;
        return pr.addColor(c, n);
    };
    const int bg = idx(st.bg, "page");
    const int axis = idx(st.axis, "axis");
    const int text = idx(st.text, "text");
    const int grid = st.gridColor.isValid() ? idx(st.gridColor, "gridline") : axis;
    // светлый цвет тёмного стиля, которым перекрашены чёрные кривые, при смене стиля
    // возвращается к чёрному (и наоборот)
    int darkText = -2;  // −2 — такого цвета в проекте нет
    if (name == "dark") {
        darkText = text;
    } else {
        for (auto it = pr.colors.constBegin(); it != pr.colors.constEnd(); ++it)
            if (it.key() > 15 && it->rgb.rgb() == styleSpec("dark").text.rgb())
                darkText = it.key();
    }
    pr.bgColor = bg;
    pr.bgFill = true;
    const int font = st.font;
    for (Graph &g : pr.graphs) {
        Props &p = g.p;
        p.set("title_font", font);
        p.set("subtitle_font", font);
        p.set("legend_font", font);
        p.set("title_size", st.titleSize);
        p.set("subtitle_size", st.subtitleSize);
        p.set("legend_char_size", st.legendSize);
        p.set("title_color", text);
        p.set("subtitle_color", text);
        p.set("legend_color", text);
        p.set("frame_type", st.frame);
        p.set("frame_pattern", st.framePattern ? 1 : 0);
        p.set("frame_color", axis);
        p.set("frame_linewidth", st.lw);
        p.set("frame_bg_pattern", st.frameBg.isValid() ? 1 : 0);
        if (st.frameBg.isValid())
            p.set("frame_bg_color", idx(st.frameBg, "plotarea"));
        p.set("legend_box_pattern", st.legendBox ? 1 : 0);
        p.set("legend_box_color", axis);
        p.set("legend_box_fill_pattern", st.legendBox ? 1 : 0);
        p.set("legend_box_fill_color", bg);
        for (int i = 0; i < 4; ++i) {
            Props &a = g.axes[i].p;
            a.set("label_font", font);
            a.set("ticklabel_font", font);
            a.set("label_char_size", st.labelSize);
            a.set("ticklabel_char_size", st.ticklabelSize);
            a.set("label_color", text);
            a.set("ticklabel_color", text);
            a.set("bar_color", axis);
            a.set("tick_inout", st.inout);
            if (i < 2)
                a.set("tick_place", st.place);
            a.set("bar_linewidth", st.lw);
            a.set("tick_major_size", st.tickMajor);
            a.set("tick_minor_size", st.tickMinor);
            if (st.minorTicks >= 0)
                a.set("tick_minor_ticks", st.minorTicks);
            else if (a.i("tick_minor_ticks") == 0)
                a.set("tick_minor_ticks", 1);
            // в Grace сетка рисуется цветом и стилем делений
            a.set("tick_major_grid", st.grid);
            a.set("tick_minor_grid", false);
            a.set("tick_major_color", st.grid ? grid : axis);
            a.set("tick_minor_color", st.grid ? grid : axis);
            a.set("tick_major_linestyle", st.grid ? st.gridStyle : 1);
            a.set("tick_minor_linestyle", 1);
            a.set("tick_major_linewidth", st.grid && st.gridLw > 0 ? st.gridLw : st.lw);
            a.set("tick_minor_linewidth", st.grid && st.gridLw > 0 ? st.gridLw : st.lw);
            // крупным шрифтом подписи не должны слипаться: не больше maxTicks делений
            const bool isX = i % 2 == 0;
            const Vec4 w = p.v4("world");
            const double range = std::fabs(isX ? w[2] - w[0] : w[3] - w[1]);
            const double step = a.d("tick_major");
            const bool lin = p.s(isX ? "xscale" : "yscale") == "Normal";
            if (st.maxTicks > 0 && lin && step > 0 && range / step > st.maxTicks) {
                const double raw = range / st.maxTicks;
                const double mag = std::pow(10.0, std::floor(std::log10(raw)));
                double nice = 10 * mag;
                for (double m : {1.0, 2.0, 2.5, 5.0})
                    if (m * mag >= raw) {
                        nice = m * mag;
                        break;
                    }
                a.set("tick_major", nice);
            }
        }
        // все кривые чёрные (например, после ч/б стиля) — вернуть разные цвета и сплошные линии
        if (!st.bw && g.sets.size() >= 2) {
            bool allBlack = true;
            for (const DataSet &s : g.sets)
                allBlack = allBlack && (s.p.i("line_color") == 1 || s.p.i("line_color") == darkText);
            if (allBlack) {
                static const int cycle[] = {4, 2, 3, 11, 8, 9, 10, 6, 12, 14, 13, 15, 5, 7};
                int c = 0;
                for (DataSet &s : g.sets) {
                    setSetColor(s, cycle[c++ % 14]);
                    s.p.set("line_linestyle", 1);
                }
            }
        }
        int k = 0;
        for (DataSet &s : g.sets) {
            if (st.setLw > 0 && s.p.d("line_linewidth") <= 3.5)
                s.p.set("line_linewidth", st.setLw);
            s.p.set("symbol_linewidth", qMin(st.lw, 1.5));
            s.p.set("errorbar_linewidth", st.lw);
            s.p.set("errorbar_riser_linewidth", st.lw);
            s.p.set("avalue_font", font);
            // чёрные кривые на тёмном фоне — светлые, и обратно
            const int lightOrBlack = name == "dark" ? darkText : 1;
            for (const char *c : {"line_color", "symbol_color", "symbol_fill_color", "errorbar_color", "avalue_color"}) {
                const int v = s.p.i(c);
                if ((v == 1 || v == darkText) && v != lightOrBlack)
                    s.p.set(c, lightOrBlack);
            }
            if (st.bw) {
                // чёрно-белый: различаем стилем линии и формой символа
                static const int styles[] = {1, 3, 4, 5, 2, 6, 7, 8};
                static const int symbols[] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
                s.p.set("line_color", 1);
                s.p.set("symbol_color", 1);
                s.p.set("errorbar_color", 1);
                s.p.set("avalue_color", 1);
                s.p.set("fill_color", 7);
                s.p.set("line_linestyle", styles[k % 8]);
                if (s.p.i("symbol") != 0 || s.p.i("line_type") == 0) {
                    s.p.set("symbol", symbols[k % 9]);
                    s.p.set("symbol_fill_pattern", 1);
                    s.p.set("symbol_fill_color", 0);
                }
            }
            if (s.p.i("symbol") != 0) {
                if (st.symbolScale != 1.0)
                    s.p.set("symbol_size", qBound(0.2, s.p.d("symbol_size") * st.symbolScale, 3.0));
                if (st.fillSymbols) {
                    s.p.set("symbol_fill_pattern", 1);
                    s.p.set("symbol_fill_color", s.p.i("symbol_color"));
                }
            }
            ++k;
        }
    }
    for (DrawObject &o : pr.objects) {
        if (o.kind == "string") {
            o.p.set("font", font);
            if (o.p.i("color") == 1 || o.p.i("color") == darkText)
                o.p.set("color", text);
        } else if (o.p.i("color") == 1 || o.p.i("color") == darkText) {
            o.p.set("color", axis);
        }
    }
}

void gradientColors(Project &pr, const QVector<QPair<int, int>> &sets, const QString &name)
{
    // viridis и «тёплый» (inferno) — равномерные по яркости шкалы
    static const QVector<QColor> viridis = {{68, 1, 84},    {72, 40, 120},  {62, 74, 137},  {49, 104, 142},
                                            {38, 130, 142}, {31, 158, 137}, {53, 183, 121}, {109, 205, 89},
                                            {180, 222, 44}, {253, 231, 37}};
    static const QVector<QColor> inferno = {{0, 0, 4},      {40, 11, 84},   {101, 21, 110}, {159, 42, 99},
                                            {212, 72, 66},  {245, 125, 21}, {250, 193, 39}, {252, 255, 164}};
    static const QVector<QColor> cool = {{8, 48, 107}, {33, 113, 181}, {66, 146, 198}, {107, 174, 214}, {158, 202, 225}};
    const QVector<QColor> &stops = name == "inferno" ? inferno : name == "blues" ? cool : viridis;
    const int n = sets.size();
    // крайне светлые концы шкал на белом плохо видны — берём 0…85 %
    const double span = name == "viridis" ? 0.92 : 0.85;
    for (int k = 0; k < n; ++k) {
        Graph *g = pr.graph(sets[k].first);
        DataSet *s = g ? g->set(sets[k].second) : nullptr;
        if (!s)
            continue;
        const double t = n > 1 ? span * k / (n - 1) : 0.0;
        const double pos = t * (stops.size() - 1);
        const int i = qMin(int(pos), int(stops.size()) - 2);
        const double f = pos - i;
        const QColor &a = stops[i], &b = stops[i + 1];
        const QColor c(qRound(a.red() + f * (b.red() - a.red())), qRound(a.green() + f * (b.green() - a.green())),
                       qRound(a.blue() + f * (b.blue() - a.blue())));
        setSetColor(*s, pr.addColor(c, QString("%1-%2").arg(name).arg(k + 1)));
    }
}

Project newProject(bool modern)
{
    Project pr;
    if (modern)
        pr.applyPalette(kModernColors);
    Graph &g = pr.graphOrCreate(0);
    for (int a = 0; a < 2; ++a) {
        g.axes[a].p.set("tick_major", 0.2);
        g.axes[a].p.set("tick_minor_ticks", 1);
    }
    g.p.setV4("world", 0, 0, 1, 1);
    if (modern)
        applyStyle(pr, "modern");
    return pr;
}

void styleImported(Project &pr, Graph &g)
{
    Q_UNUSED(pr);
    for (int a = 0; a < 2; ++a)
        g.axes[a].p.set("tick_minor_ticks", 1);
    for (DataSet &s : g.sets) {
        if (s.p.i("line_type") == 1 && s.size() < 30 && s.p.i("symbol") == 0) {
            s.p.set("symbol", 1);
            s.p.set("symbol_size", 0.6);
            s.p.set("symbol_fill_pattern", 1);
            s.p.set("symbol_fill_color", s.p.i("symbol_color"));
        }
        s.p.set("line_linewidth", qMax(s.p.d("line_linewidth"), 1.5));
    }
}

// ---------------------------------------------------------------------------
// Автомасштаб

bool setBounds(const DataSet &s, const Graph *g, double &xmin, double &xmax, double &ymin, double &ymax)
{
    const int n = s.size();
    if (n == 0)
        return false;
    const QString t = s.type();
    const bool logx = g && g->p.s("xscale") == "Logarithmic";
    const bool logy = g && g->p.s("yscale") == "Logarithmic";
    xmin = ymin = 1e300;
    xmax = ymax = -1e300;
    auto ax = [&](double v) {
        if (std::isfinite(v) && (!logx || v > 0)) {
            xmin = qMin(xmin, v);
            xmax = qMax(xmax, v);
        }
    };
    auto ay = [&](double v) {
        if (std::isfinite(v) && (!logy || v > 0)) {
            ymin = qMin(ymin, v);
            ymax = qMax(ymax, v);
        }
    };
    const auto &x = s.col(0), &y = s.col(1);
    auto c = [&](int k, int i) { return s.col(k).value(i, 0.0); };
    for (int i = 0; i < n; ++i) {
        const double xi = x[i], yi = y.value(i, 0.0);
        ax(xi);
        ay(yi);
        if (t == "xydx") { ax(xi - std::fabs(c(2, i))); ax(xi + std::fabs(c(2, i))); }
        else if (t == "xydy" || t == "bardy") { ay(yi - std::fabs(c(2, i))); ay(yi + std::fabs(c(2, i))); }
        else if (t == "xydxdx") { ax(xi + std::fabs(c(2, i))); ax(xi - std::fabs(c(3, i))); }
        else if (t == "xydydy" || t == "bardydy") { ay(yi + std::fabs(c(2, i))); ay(yi - std::fabs(c(3, i))); }
        else if (t == "xydxdy") {
            ax(xi - std::fabs(c(2, i))); ax(xi + std::fabs(c(2, i)));
            ay(yi - std::fabs(c(3, i))); ay(yi + std::fabs(c(3, i)));
        } else if (t == "xydxdxdydy") {
            ax(xi + std::fabs(c(2, i))); ax(xi - std::fabs(c(3, i)));
            ay(yi + std::fabs(c(4, i))); ay(yi - std::fabs(c(5, i)));
        } else if (t == "xyhilo") { ay(c(2, i)); ay(c(3, i)); ay(c(4, i)); }
        else if (t == "xyboxplot") { ay(c(2, i)); ay(c(3, i)); ay(c(4, i)); ay(c(5, i)); }
        else if (t == "xyr") {
            ax(xi - std::fabs(c(2, i))); ax(xi + std::fabs(c(2, i)));
            ay(yi - std::fabs(c(2, i))); ay(yi + std::fabs(c(2, i)));
        } else if (t == "xyvmap") { ax(xi + c(2, i)); ay(yi + c(3, i)); }
    }
    if (t.startsWith("bar") && !logy)
        ay(0.0);
    return xmin <= xmax && ymin <= ymax;
}

static void niceRange(double lo, double hi, bool log, int nticks, double &a, double &b, double &step)
{
    if (log) {
        lo = qMax(lo, 1e-300);
        hi = qMax(hi, lo);
        a = std::pow(10.0, std::floor(std::log10(lo)));
        b = std::pow(10.0, std::ceil(std::log10(hi)));
        if (a == b) {
            a /= 10;
            b *= 10;
        }
        step = 10.0;
        return;
    }
    if (lo == hi) {
        double d = lo != 0 ? std::fabs(lo) * 0.1 : 1.0;
        lo -= d;
        hi += d;
    }
    step = niceStep(hi - lo, qMax(2, nticks - 1));
    a = std::floor(lo / step + 1e-9) * step;
    b = std::ceil(hi / step - 1e-9) * step;
    step = niceStep(b - a, qMax(2, nticks - 1));
    a = std::floor(lo / step + 1e-9) * step;
    b = std::ceil(hi / step - 1e-9) * step;
    if (std::fabs(a) < step * 1e-9) a = 0;
    if (std::fabs(b) < step * 1e-9) b = 0;
}

bool autoscaleGraph(Graph &g, const QString &axes, const QVector<int> &sets)
{
    double xmin = 1e300, xmax = -1e300, ymin = 1e300, ymax = -1e300;
    bool any = false;
    for (const DataSet &s : g.sets) {
        if (!sets.isEmpty() && !sets.contains(s.id))
            continue;
        if (sets.isEmpty() && s.hidden())
            continue;
        double a, b, c, d;
        if (!setBounds(s, &g, a, b, c, d))
            continue;
        xmin = qMin(xmin, a);
        xmax = qMax(xmax, b);
        ymin = qMin(ymin, c);
        ymax = qMax(ymax, d);
        any = true;
    }
    if (!any)
        return false;
    if (g.type() == "Chart" && !g.sets.isEmpty()) {
        xmin -= 0.5;
        xmax += 0.5;
        if (g.hp.b("stacked")) {
            // сумма по столбцам
            QVector<double> sum;
            for (const DataSet &s : g.sets) {
                if (s.hidden()) continue;
                const auto &y = s.col(1);
                if (sum.size() < y.size()) sum.resize(y.size());
                for (int i = 0; i < y.size(); ++i)
                    if (std::isfinite(y[i])) sum[i] += y[i];
            }
            for (double v : sum) {
                ymin = qMin(ymin, v);
                ymax = qMax(ymax, v);
            }
            ymin = qMin(ymin, 0.0);
        }
    }
    Vec4 w = g.p.v4("world");
    if (axes.contains('x')) {
        double a, b, step;
        niceRange(xmin, xmax, g.p.s("xscale") == "Logarithmic", g.axes[0].p.i("tick_default"), a, b, step);
        w[0] = a;
        w[2] = b;
        for (int ai : {0, 2}) {
            g.axes[ai].p.set("tick_major", step);
            if (g.p.s("xscale") == "Logarithmic")
                autoTickAxis(g, ai);
        }
    }
    if (axes.contains('y')) {
        double a, b, step;
        niceRange(ymin, ymax, g.p.s("yscale") == "Logarithmic", g.axes[1].p.i("tick_default"), a, b, step);
        w[1] = a;
        w[3] = b;
        for (int ai : {1, 3}) {
            g.axes[ai].p.set("tick_major", step);
        }
    }
    g.p.setV4("world", w[0], w[1], w[2], w[3]);
    if (g.p.s("xscale") == "Logarithmic")
        for (int ai : {0, 2}) autoTickAxis(g, ai);
    if (g.p.s("yscale") == "Logarithmic")
        for (int ai : {1, 3}) autoTickAxis(g, ai);
    return true;
}

void updateTicksForWorld(Graph &g)
{
    for (int ai = 0; ai < 4; ++ai)
        autoTickAxis(g, ai);
}

void arrangeGraphs(Project &pr, int rows, int cols, double left, double right, double bottom,
                   double top, double hgap, double vgap, bool addMissing)
{
    rows = qMax(1, rows);
    cols = qMax(1, cols);
    const int need = rows * cols;
    if (addMissing) {
        while (pr.graphs.size() < need) {
            Graph ng(pr.nextGraphId());
            if (!pr.graphs.isEmpty()) {
                // оформление как у первого графика, но без данных
                Graph copy = pr.graphs.first();
                copy.id = ng.id;
                copy.sets.clear();
                copy.p.set("title", QString());
                copy.p.set("subtitle", QString());
                ng = copy;
            }
            pr.graphs.append(ng);
        }
    }
    const double W = pr.viewW(), H = pr.viewH();
    const double cw = (W - left - right - hgap * (cols - 1)) / cols;
    const double ch = (H - bottom - top - vgap * (rows - 1)) / rows;
    int k = 0;
    for (Graph &g : pr.graphs) {
        if (k >= need)
            break;
        const int r = k / cols, c = k % cols;
        const double x1 = left + c * (cw + hgap);
        const double y2 = H - top - r * (ch + vgap);
        g.p.setV4("view", x1, y2 - ch, x1 + cw, y2);
        g.hp.set("on", true);
        g.hp.set("hidden", false);
        ++k;
    }
}

// ---------------------------------------------------------------------------
// Формулы

static std::function<const QVector<double> *(const QString &)> makeResolver(Graph &g,
                                                                         QVector<QVector<double>> &keep)
{
    return [&g, &keep](const QString &name) -> const QVector<double> * {
        // name: sN.col или gM.sN.col (другие графики недоступны — берём текущий)
        QString n = name;
        int dot = n.indexOf('.');
        if (n.startsWith('g')) {
            n = n.mid(dot + 1);
            dot = n.indexOf('.');
        }
        const int sid = n.mid(1, dot - 1).toInt();
        const QString col = n.mid(dot + 1);
        const DataSet *s = g.set(sid);
        if (!s)
            return nullptr;
        static const QStringList cols = {"x", "y", "y1", "y2", "y3", "y4"};
        int k = col == "z" ? 2 : cols.indexOf(col);
        if (k < 0 || k >= s->cols.size())
            return nullptr;
        keep.append(s->cols[k]);  // копия (данные разделяются неявно)
        return &keep.last();
    };
}

bool applyFormula(Graph &g, DataSet &s, const QString &text, QString *error)
{
    QVector<Expr::Assignment> as;
    if (!Expr::splitAssignments(text, as, error))
        return false;
    static const QStringList targets = {"x", "y", "y1", "y2", "y3", "y4"};
    QVector<QVector<double>> cols = s.cols;
    const int n = s.size();
    QVector<QVector<double>> keep;
    keep.reserve(64);
    auto resolver = makeResolver(g, keep);
    for (const auto &a : as) {
        Expr e;
        if (!e.parse(a.expr, error))
            return false;
        const int k = targets.indexOf(a.target);
        if (k >= setTypeColumns(s.type())) {
            if (error)
                *error = QStringLiteral("у набора типа %1 нет столбца %2").arg(s.type(), a.target);
            return false;
        }
        QVector<const QVector<double> *> ptrs;
        for (const auto &c : cols)
            ptrs << &c;
        Expr::Ctx c;
        c.cols = &ptrs;
        c.n = n;
        c.resolver = resolver;
        QVector<double> out(n);
        for (int i = 0; i < n; ++i) {
            c.row = i;
            out[i] = e.eval(c);
        }
        cols[k] = out;  // следующие присваивания видят новое значение
    }
    s.cols = cols;
    return true;
}

int createFromFormula(Graph &g, double xmin, double xmax, int npts, const QString &yexpr, QString *error,
                      const QString &xexpr)
{
    npts = qMax(2, npts);
    QVector<double> t(npts), x(npts), y;
    for (int i = 0; i < npts; ++i)
        t[i] = xmin + (xmax - xmin) * i / (npts - 1);
    if (!xexpr.trimmed().isEmpty()) {
        if (!evalOnArray(xexpr, t, x, error))
            return -1;
    } else {
        x = t;
    }
    // y может ссылаться на x и на индекс
    Expr e;
    if (!e.parse(yexpr, error))
        return -1;
    QVector<const QVector<double> *> ptrs{&x};
    Expr::Ctx c;
    c.cols = &ptrs;
    c.n = npts;
    y.resize(npts);
    for (int i = 0; i < npts; ++i) {
        c.row = i;
        c.t = t[i];
        y[i] = e.eval(c);
    }
    DataSet &s = g.newSet("xy");
    s.cols = {x, y};
    s.p.set("comment", QStringLiteral("y = %1").arg(yexpr));
    return s.id;
}

int addTableAsSets(Graph &g, const Table &t, const QString &mode, const QString &type,
                   const QVector<int> &columns)
{
    const int nc = t.cols.size();
    if (nc == 0)
        return -1;
    int first = -1;
    if (mode == "nxy") {
        if (nc == 1) {
            DataSet &s = g.newSet("xy");
            QVector<double> idx(t.rows());
            std::iota(idx.begin(), idx.end(), 0.0);
            s.cols = {idx, t.cols[0]};
            return s.id;
        }
        for (int k = 1; k < nc; ++k) {
            DataSet &s = g.newSet("xy");
            s.cols = {t.cols[0], t.cols[k]};
            if (k - 1 < t.headers.size() || k < t.headers.size())
                s.p.set("legend", t.headers.value(k));
            if (first < 0)
                first = s.id;
        }
        return first;
    }
    // один набор заданного типа из выбранных столбцов
    DataSet &s = g.newSet(type);
    const int need = setTypeColumns(type);
    QVector<QVector<double>> cols;
    for (int k = 0; k < need; ++k) {
        int src = k < columns.size() ? columns[k] : k;
        cols << (src >= 0 && src < nc ? t.cols[src] : QVector<double>(t.rows(), 0.0));
    }
    s.cols = cols;
    if (!columns.isEmpty() && columns.value(1) < t.headers.size())
        s.p.set("legend", t.headers.value(columns.value(1)));
    return s.id;
}

int duplicateSet(Graph &g, int sid)
{
    const DataSet *src = g.set(sid);
    if (!src)
        return -1;
    DataSet copy = *src;
    copy.id = g.nextSetId();
    g.sets.append(copy);
    return copy.id;
}

static DataSet &derived(Graph &g, const DataSet &src, const QVector<double> &x, const QVector<double> &y,
                        const QString &comment)
{
    Q_UNUSED(src);
    DataSet &s = g.newSet("xy");
    s.cols = {x, y};
    s.p.set("comment", comment);
    return s;
}

// ---------------------------------------------------------------------------
// Линейная алгебра: МНК через отражения Хаусхолдера

static bool lstsq(QVector<QVector<double>> A /* m×n по строкам */, QVector<double> b, QVector<double> &x)
{
    const int m = A.size();
    if (m == 0)
        return false;
    const int n = A[0].size();
    if (m < n)
        return false;
    // масштабирование столбцов
    QVector<double> scale(n, 1.0);
    for (int j = 0; j < n; ++j) {
        double s = 0;
        for (int i = 0; i < m; ++i)
            s += A[i][j] * A[i][j];
        s = std::sqrt(s);
        scale[j] = s > 0 ? s : 1.0;
        for (int i = 0; i < m; ++i)
            A[i][j] /= scale[j];
    }
    for (int k = 0; k < n; ++k) {
        double norm = 0;
        for (int i = k; i < m; ++i)
            norm += A[i][k] * A[i][k];
        norm = std::sqrt(norm);
        if (norm == 0)
            return false;
        const double alpha = A[k][k] > 0 ? -norm : norm;
        QVector<double> v(m, 0.0);
        v[k] = A[k][k] - alpha;
        for (int i = k + 1; i < m; ++i)
            v[i] = A[i][k];
        double vv = 0;
        for (int i = k; i < m; ++i)
            vv += v[i] * v[i];
        if (vv == 0)
            continue;
        for (int j = k; j < n; ++j) {
            double s = 0;
            for (int i = k; i < m; ++i)
                s += v[i] * A[i][j];
            s = 2 * s / vv;
            for (int i = k; i < m; ++i)
                A[i][j] -= s * v[i];
        }
        double s = 0;
        for (int i = k; i < m; ++i)
            s += v[i] * b[i];
        s = 2 * s / vv;
        for (int i = k; i < m; ++i)
            b[i] -= s * v[i];
    }
    x = QVector<double>(n, 0.0);
    for (int k = n - 1; k >= 0; --k) {
        double s = b[k];
        for (int j = k + 1; j < n; ++j)
            s -= A[k][j] * x[j];
        if (std::fabs(A[k][k]) < 1e-300)
            return false;
        x[k] = s / A[k][k];
    }
    for (int j = 0; j < n; ++j)
        x[j] /= scale[j];
    return true;
}

static bool solveLinear(QVector<QVector<double>> M, QVector<double> r, QVector<double> &x)
{
    const int n = M.size();
    for (int k = 0; k < n; ++k) {
        int piv = k;
        for (int i = k + 1; i < n; ++i)
            if (std::fabs(M[i][k]) > std::fabs(M[piv][k]))
                piv = i;
        if (std::fabs(M[piv][k]) < 1e-300)
            return false;
        std::swap(M[k], M[piv]);
        std::swap(r[k], r[piv]);
        for (int i = k + 1; i < n; ++i) {
            const double f = M[i][k] / M[k][k];
            for (int j = k; j < n; ++j)
                M[i][j] -= f * M[k][j];
            r[i] -= f * r[k];
        }
    }
    x = QVector<double>(n, 0.0);
    for (int k = n - 1; k >= 0; --k) {
        double s = r[k];
        for (int j = k + 1; j < n; ++j)
            s -= M[k][j] * x[j];
        x[k] = s / M[k][k];
    }
    return true;
}

static QString g6(double v) { return QString::number(v, 'g', 6); }

FitResult regression(Graph &g, int sid, const QString &kind, int degree, int npts)
{
    FitResult res;
    const DataSet *src = g.set(sid);
    if (!src) {
        res.message = T("Набор не найден");
        return res;
    }
    QVector<double> X, Y;
    const auto &x = src->col(0), &y = src->col(1);
    for (int i = 0; i < x.size() && i < y.size(); ++i) {
        if (!std::isfinite(x[i]) || !std::isfinite(y[i]))
            continue;
        if ((kind == "exp" || kind == "power") && y[i] <= 0)
            continue;
        if ((kind == "power" || kind == "log") && x[i] <= 0)
            continue;
        X << x[i];
        Y << y[i];
    }
    const int m = X.size();
    int nparam = kind == "poly" ? degree + 1 : 2;
    if (kind == "poly")
        degree = qBound(1, degree, 10);
    if (m < nparam) {
        res.message = T("Слишком мало точек для аппроксимации");
        return res;
    }
    QVector<QVector<double>> A(m);
    QVector<double> b(m);
    for (int i = 0; i < m; ++i) {
        if (kind == "poly" || kind == "linear") {
            const int d = kind == "linear" ? 1 : degree;
            A[i].resize(d + 1);
            double p = 1;
            for (int k = 0; k <= d; ++k) {
                A[i][k] = p;
                p *= X[i];
            }
            b[i] = Y[i];
        } else if (kind == "exp") {
            A[i] = {1.0, X[i]};
            b[i] = std::log(Y[i]);
        } else if (kind == "power") {
            A[i] = {1.0, std::log(X[i])};
            b[i] = std::log(Y[i]);
        } else {  // log
            A[i] = {1.0, std::log(X[i])};
            b[i] = Y[i];
        }
    }
    QVector<double> c;
    if (!lstsq(A, b, c)) {
        res.message = T("Не удалось решить систему (вырожденные данные)");
        return res;
    }
    auto model = [&](double xv) {
        if (kind == "exp") return std::exp(c[0]) * std::exp(c[1] * xv);
        if (kind == "power") return std::exp(c[0]) * std::pow(xv, c[1]);
        if (kind == "log") return c[0] + c[1] * std::log(xv);
        double s = 0, p = 1;
        for (double ck : c) {
            s += ck * p;
            p *= xv;
        }
        return s;
    };
    double mean = std::accumulate(Y.begin(), Y.end(), 0.0) / m;
    double ssr = 0, sst = 0;
    for (int i = 0; i < m; ++i) {
        const double r = Y[i] - model(X[i]);
        ssr += r * r;
        sst += (Y[i] - mean) * (Y[i] - mean);
    }
    res.r2 = sst > 0 ? 1 - ssr / sst : 1.0;
    res.chi2 = ssr;
    QString formula;
    if (kind == "exp") {
        res.params = {std::exp(c[0]), c[1]};
        formula = QStringLiteral("y = %1·exp(%2·x)").arg(g6(res.params[0]), g6(res.params[1]));
    } else if (kind == "power") {
        res.params = {std::exp(c[0]), c[1]};
        formula = QStringLiteral("y = %1·x^%2").arg(g6(res.params[0]), g6(res.params[1]));
    } else if (kind == "log") {
        res.params = c;
        formula = QStringLiteral("y = %1 + %2·ln(x)").arg(g6(c[0]), g6(c[1]));
    } else {
        res.params = c;
        QStringList terms;
        for (int k = 0; k < c.size(); ++k) {
            QString t = g6(c[k]);
            if (k == 1) t += "·x";
            else if (k > 1) t += QStringLiteral("·x^%1").arg(k);
            terms << t;
        }
        formula = "y = " + terms.join(" + ");
        formula.replace("+ -", "− ");
    }
    double lo = *std::min_element(X.begin(), X.end()), hi = *std::max_element(X.begin(), X.end());
    npts = qMax(2, npts);
    QVector<double> fx(npts), fy(npts);
    const bool logx = g.p.s("xscale") == "Logarithmic" && lo > 0;
    for (int i = 0; i < npts; ++i) {
        fx[i] = logx ? lo * std::pow(hi / lo, double(i) / (npts - 1)) : lo + (hi - lo) * i / (npts - 1);
        fy[i] = model(fx[i]);
    }
    DataSet &s = derived(g, *src, fx, fy, formula);
    s.p.set("line_linestyle", 1);
    s.p.set("line_linewidth", 1.5);
    s.p.set("legend", T("Аппроксимация S%1").arg(sid));
    res.newSet = s.id;
    res.ok = true;
    res.message = formula + T("\nR² = %1\nСумма квадратов остатков = %2\nТочек: %3")
                                .arg(QString::number(res.r2, 'f', 6), g6(ssr))
                                .arg(m);
    return res;
}

FitResult nonlinearFit(Graph &g, int sid, const QString &exprText, const QVector<double> &p0, int maxIter,
                       int npts, double xmin, double xmax)
{
    FitResult res;
    const DataSet *src = g.set(sid);
    if (!src) {
        res.message = T("Набор не найден");
        return res;
    }
    Expr e;
    QString err;
    QString body = exprText.trimmed();
    // допускаем «y = …»
    if (body.startsWith("y", Qt::CaseInsensitive)) {
        int eq = body.indexOf('=');
        if (eq > 0 && body.left(eq).trimmed().toLower() == "y" && body.mid(eq + 1, 1) != "=")
            body = body.mid(eq + 1).trimmed();
    }
    if (!e.parse(body, &err)) {
        res.message = err;
        return res;
    }
    QVector<int> used;
    for (int k = 0; k < 10; ++k)
        if (e.usesParam(k))
            used << k;
    if (used.isEmpty()) {
        res.message = T("В формуле нет параметров a0…a9");
        return res;
    }
    QVector<double> X, Y, W;
    const auto &x = src->col(0), &y = src->col(1);
    for (int i = 0; i < x.size() && i < y.size(); ++i) {
        if (!std::isfinite(x[i]) || !std::isfinite(y[i]))
            continue;
        if (xmin < xmax && (x[i] < xmin || x[i] > xmax))
            continue;
        X << x[i];
        Y << y[i];
    }
    const int m = X.size(), np = used.size();
    if (m < np) {
        res.message = T("Точек меньше, чем параметров");
        return res;
    }
    double params[10] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
    for (int k = 0; k < 10 && k < p0.size(); ++k)
        params[k] = p0[k];
    QVector<const QVector<double> *> ptrs{&X};
    auto evalAll = [&](const double *pp, QVector<double> &out) {
        Expr::Ctx c;
        c.cols = &ptrs;
        c.n = m;
        c.params = pp;
        out.resize(m);
        for (int i = 0; i < m; ++i) {
            c.row = i;
            out[i] = e.eval(c);
        }
    };
    auto ssq = [&](const double *pp) {
        QVector<double> f;
        evalAll(pp, f);
        double s = 0;
        for (int i = 0; i < m; ++i) {
            const double r = Y[i] - f[i];
            s += r * r;
        }
        return std::isfinite(s) ? s : 1e300;
    };
    double lambda = 1e-3;
    double cur = ssq(params);
    int iter = 0;
    bool converged = false;
    for (; iter < maxIter; ++iter) {
        QVector<double> f0;
        evalAll(params, f0);
        // якобиан численно
        QVector<QVector<double>> J(m, QVector<double>(np));
        for (int j = 0; j < np; ++j) {
            double pp[10];
            std::copy(params, params + 10, pp);
            const double h = 1e-7 * qMax(1e-7, std::fabs(params[used[j]]));
            pp[used[j]] += h;
            QVector<double> f1;
            evalAll(pp, f1);
            for (int i = 0; i < m; ++i)
                J[i][j] = (f1[i] - f0[i]) / h;
        }
        QVector<QVector<double>> JTJ(np, QVector<double>(np, 0.0));
        QVector<double> JTr(np, 0.0);
        for (int i = 0; i < m; ++i) {
            const double r = Y[i] - f0[i];
            for (int a = 0; a < np; ++a) {
                JTr[a] += J[i][a] * r;
                for (int b = 0; b < np; ++b)
                    JTJ[a][b] += J[i][a] * J[i][b];
            }
        }
        bool improved = false;
        for (int tries = 0; tries < 20; ++tries) {
            auto M = JTJ;
            for (int a = 0; a < np; ++a)
                M[a][a] += lambda * (JTJ[a][a] > 0 ? JTJ[a][a] : 1e-12);
            QVector<double> delta;
            if (!solveLinear(M, JTr, delta)) {
                lambda *= 10;
                continue;
            }
            double trial[10];
            std::copy(params, params + 10, trial);
            for (int a = 0; a < np; ++a)
                trial[used[a]] += delta[a];
            const double s = ssq(trial);
            if (s < cur) {
                const double rel = (cur - s) / qMax(cur, 1e-300);
                std::copy(trial, trial + 10, params);
                cur = s;
                lambda = qMax(lambda / 10, 1e-12);
                improved = true;
                if (rel < 1e-10)
                    converged = true;
                break;
            }
            lambda *= 10;
        }
        if (!improved || converged) {
            converged = true;
            break;
        }
    }
    // график подобранной функции
    double lo = *std::min_element(X.begin(), X.end()), hi = *std::max_element(X.begin(), X.end());
    npts = qMax(2, npts);
    QVector<double> fx(npts), fy;
    for (int i = 0; i < npts; ++i)
        fx[i] = lo + (hi - lo) * i / (npts - 1);
    {
        QVector<const QVector<double> *> pf{&fx};
        Expr::Ctx c;
        c.cols = &pf;
        c.n = npts;
        c.params = params;
        fy.resize(npts);
        for (int i = 0; i < npts; ++i) {
            c.row = i;
            fy[i] = e.eval(c);
        }
    }
    double mean = std::accumulate(Y.begin(), Y.end(), 0.0) / m, sst = 0;
    for (double v : Y)
        sst += (v - mean) * (v - mean);
    res.r2 = sst > 0 ? 1 - cur / sst : 1.0;
    res.chi2 = cur;
    res.params = QVector<double>(params, params + 10);
    QString fitted = body;
    QStringList lines;
    for (int k : used) {
        lines << QStringLiteral("a%1 = %2").arg(k).arg(QString::number(params[k], 'g', 8));
        fitted.replace(QRegularExpression(QStringLiteral("\\ba%1\\b").arg(k)), QString::number(params[k], 'g', 6));
    }
    DataSet &s = derived(g, *src, fx, fy, "y = " + fitted);
    s.p.set("legend", T("Аппроксимация S%1").arg(sid));
    res.newSet = s.id;
    res.ok = true;
    res.message = lines.join('\n') + T("\n\nR² = %1\nχ² (сумма квадратов) = %2\nИтераций: %3%4")
                                        .arg(QString::number(res.r2, 'f', 6), g6(cur))
                                        .arg(iter + 1)
                                        .arg(converged ? QString() : T(" (не сошлось — увеличьте число итераций)"));
    return res;
}

// ---------------------------------------------------------------------------
// Преобразования

int runningAverage(Graph &g, int sid, int window)
{
    const DataSet *src = g.set(sid);
    if (!src || src->size() == 0)
        return -1;
    window = qMax(1, window);
    const auto &x = src->col(0), &y = src->col(1);
    const int n = x.size();
    QVector<double> ox, oy;
    for (int i = 0; i + window <= n; ++i) {
        double sx = 0, sy = 0;
        for (int k = i; k < i + window; ++k) {
            sx += x[k];
            sy += y[k];
        }
        ox << sx / window;
        oy << sy / window;
    }
    return derived(g, *src, ox, oy, T("Скользящее среднее S%1, окно %2").arg(sid).arg(window)).id;
}

int derivative(Graph &g, int sid)
{
    const DataSet *src = g.set(sid);
    if (!src || src->size() < 2)
        return -1;
    const auto &x = src->col(0), &y = src->col(1);
    const int n = x.size();
    QVector<double> ox(n), oy(n);
    for (int i = 0; i < n; ++i) {
        ox[i] = x[i];
        if (i == 0)
            oy[i] = (y[1] - y[0]) / (x[1] - x[0]);
        else if (i == n - 1)
            oy[i] = (y[n - 1] - y[n - 2]) / (x[n - 1] - x[n - 2]);
        else
            oy[i] = (y[i + 1] - y[i - 1]) / (x[i + 1] - x[i - 1]);
    }
    return derived(g, *src, ox, oy, T("Производная S%1").arg(sid)).id;
}

int integral(Graph &g, int sid, double *total)
{
    const DataSet *src = g.set(sid);
    if (!src || src->size() < 1)
        return -1;
    const auto &x = src->col(0), &y = src->col(1);
    const int n = x.size();
    QVector<double> oy(n, 0.0);
    for (int i = 1; i < n; ++i)
        oy[i] = oy[i - 1] + 0.5 * (y[i] + y[i - 1]) * (x[i] - x[i - 1]);
    if (total)
        *total = oy.isEmpty() ? 0 : oy.last();
    return derived(g, *src, x, oy, T("Интеграл S%1").arg(sid)).id;
}

void sortSet(DataSet &s, bool byY, bool descending)
{
    const int n = s.size();
    QVector<int> idx(n);
    std::iota(idx.begin(), idx.end(), 0);
    const auto &key = s.col(byY ? 1 : 0);
    std::stable_sort(idx.begin(), idx.end(), [&](int a, int b) {
        return descending ? key[a] > key[b] : key[a] < key[b];
    });
    for (auto &c : s.cols) {
        QVector<double> nc(n);
        for (int i = 0; i < n; ++i)
            nc[i] = c.value(idx[i]);
        c = nc;
    }
    if (!s.strings.isEmpty()) {
        QStringList ns;
        for (int i = 0; i < n; ++i)
            ns << s.strings.value(idx[i]);
        s.strings = ns;
    }
}

static void fft(QVector<std::complex<double>> &a)
{
    const int n = a.size();
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(a[i], a[j]);
    }
    for (int len = 2; len <= n; len <<= 1) {
        const double ang = -2 * M_PI / len;
        const std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (int i = 0; i < n; i += len) {
            std::complex<double> w(1);
            for (int j = 0; j < len / 2; ++j) {
                auto u = a[i + j], v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

int fourier(Graph &g, int sid, bool power)
{
    const DataSet *src = g.set(sid);
    if (!src || src->size() < 4)
        return -1;
    const auto &x = src->col(0), &y = src->col(1);
    const int n = y.size();
    const double dx = (x.last() - x.first()) / qMax(1, n - 1);
    int N = 1;
    while (N < n)
        N <<= 1;
    QVector<std::complex<double>> a(N, 0.0);
    // окно Ханна снижает «растекание» спектра
    for (int i = 0; i < n; ++i)
        a[i] = y[i] * (n > 1 ? 0.5 * (1 - std::cos(2 * M_PI * i / (n - 1))) : 1.0);
    fft(a);
    QVector<double> fx, fy;
    for (int k = 0; k <= N / 2; ++k) {
        fx << (dx != 0 ? k / (N * dx) : k);
        const double amp = std::abs(a[k]) * 2.0 / n;
        fy << (power ? amp * amp : amp);
    }
    return derived(g, *src, fx, fy, T("%1 спектр S%2").arg(power ? T("Мощностной") : T("Амплитудный")).arg(sid)).id;
}

int histogram(Graph &g, int sid, int bins, double lo, double hi, bool normalize)
{
    const DataSet *src = g.set(sid);
    if (!src || src->size() == 0)
        return -1;
    const auto &y = src->col(1);
    if (!(hi > lo)) {
        lo = 1e300;
        hi = -1e300;
        for (double v : y)
            if (std::isfinite(v)) {
                lo = qMin(lo, v);
                hi = qMax(hi, v);
            }
        if (!(hi > lo)) {
            lo -= 0.5;
            hi += 0.5;
        }
    }
    bins = qMax(1, bins);
    QVector<double> cx(bins), cy(bins, 0.0);
    const double w = (hi - lo) / bins;
    for (int b = 0; b < bins; ++b)
        cx[b] = lo + (b + 0.5) * w;
    int total = 0;
    for (double v : y) {
        if (!std::isfinite(v) || v < lo || v > hi)
            continue;
        int b = qMin(bins - 1, int((v - lo) / w));
        cy[b] += 1;
        ++total;
    }
    if (normalize && total > 0)
        for (double &v : cy)
            v /= total * w;
    DataSet &s = g.newSet("bar");
    s.cols = {cx, cy};
    s.p.set("comment", T("Гистограмма S%1").arg(sid));
    s.p.set("line_type", 0);
    s.p.set("symbol_fill_pattern", 1);
    s.p.set("symbol_fill_color", s.p.i("line_color"));
    s.p.set("symbol_color", 1);
    // ширина столбцов по ширине бина — примерно; точная ширина зависит от мира
    return s.id;
}

int interpolate(Graph &g, int sid, int npts, bool spline)
{
    const DataSet *src = g.set(sid);
    if (!src || src->size() < 2)
        return -1;
    DataSet sorted = *src;
    sortSet(sorted, false, false);
    const auto &x = sorted.col(0), &y = sorted.col(1);
    const int n = x.size();
    npts = qMax(2, npts);
    QVector<double> M(n, 0.0);  // вторые производные (естественный сплайн)
    if (spline && n > 2) {
        QVector<double> a(n), b(n), c(n), d(n);
        for (int i = 1; i < n - 1; ++i) {
            const double h0 = x[i] - x[i - 1], h1 = x[i + 1] - x[i];
            a[i] = h0;
            b[i] = 2 * (h0 + h1);
            c[i] = h1;
            d[i] = 6 * ((y[i + 1] - y[i]) / (h1 != 0 ? h1 : 1e-300) - (y[i] - y[i - 1]) / (h0 != 0 ? h0 : 1e-300));
        }
        // прогонка
        for (int i = 2; i < n - 1; ++i) {
            const double m = a[i] / b[i - 1];
            b[i] -= m * c[i - 1];
            d[i] -= m * d[i - 1];
        }
        for (int i = n - 2; i >= 1; --i)
            M[i] = (d[i] - (i + 1 < n - 1 ? c[i] * M[i + 1] : 0)) / b[i];
    }
    QVector<double> ox(npts), oy(npts);
    int k = 0;
    for (int i = 0; i < npts; ++i) {
        const double xv = x.first() + (x.last() - x.first()) * i / (npts - 1);
        while (k < n - 2 && xv > x[k + 1])
            ++k;
        const double h = x[k + 1] - x[k];
        const double t = h != 0 ? (xv - x[k]) / h : 0;
        double v = y[k] + t * (y[k + 1] - y[k]);
        if (spline && n > 2) {
            const double A = 1 - t, B = t;
            v = A * y[k] + B * y[k + 1] + ((A * A * A - A) * M[k] + (B * B * B - B) * M[k + 1]) * h * h / 6.0;
        }
        ox[i] = xv;
        oy[i] = v;
    }
    return derived(g, *src, ox, oy, T("%1 интерполяция S%2").arg(spline ? T("Сплайн-") : T("Линейная ")).arg(sid)).id;
}

QString statistics(const DataSet &s)
{
    QStringList out;
    const QStringList names = setColumnNames(s.type());
    out << T("Точек: %1").arg(s.size());
    for (int k = 0; k < s.cols.size(); ++k) {
        const auto &c = s.cols[k];
        double mn = 1e300, mx = -1e300, sum = 0, sum2 = 0;
        int n = 0;
        for (double v : c)
            if (std::isfinite(v)) {
                mn = qMin(mn, v);
                mx = qMax(mx, v);
                sum += v;
                sum2 += v * v;
                ++n;
            }
        if (n == 0)
            continue;
        const double mean = sum / n;
        const double sd = n > 1 ? std::sqrt(qMax(0.0, (sum2 - n * mean * mean) / (n - 1))) : 0.0;
        out << T("%1:  мин %2   макс %3   среднее %4   σ %5")
                   .arg(names.value(k), g6(mn), g6(mx), g6(mean), g6(sd));
    }
    return out.join('\n');
}

}  // namespace ng

namespace ng {

void setSetColor(DataSet &s, int color)
{
    s.p.set("line_color", color);
    s.p.set("symbol_color", color);
    s.p.set("symbol_fill_color", color);
    s.p.set("errorbar_color", color);
    if (s.type().startsWith("bar"))
        s.p.set("symbol_fill_color", color);
}

int fillToAxisMode(const DataSet &s)
{
    if (s.p.i("fill_type") == 0 || s.p.i("fill_pattern") == 0)
        return 0;
    if (s.p.i("fill_type") != 2)
        return -1;
    switch (s.p.i("baseline_type")) {
    case 0: return 1;
    case 3: return 2;
    case 4: return 3;
    default: return -1;
    }
}

void fillToAxis(Project &pr, int gid, int sid, int mode)
{
    Graph *g = pr.graph(gid);
    DataSet *s = g ? g->set(sid) : nullptr;
    if (!s)
        return;
    if (mode <= 0) {
        s->p.set("fill_type", 0);
        return;
    }
    s->p.set("fill_type", 2);
    s->p.set("baseline_type", mode == 1 ? 0 : mode == 2 ? 3 : 4);
    s->p.set("fill_pattern", 1);
    s->p.set("fill_rule", 0);
    if (s->p.i("line_type") == 0)
        s->p.set("line_type", 1);
    // светлый оттенок цвета линии: в Grace нет прозрачности, поэтому смешиваем с белым
    const QColor c = pr.color(s->p.i("line_color"));
    const double k = 0.7;
    const QColor light(int(c.red() + (255 - c.red()) * k), int(c.green() + (255 - c.green()) * k),
                       int(c.blue() + (255 - c.blue()) * k));
    const QString base = pr.colors.contains(s->p.i("line_color")) ? pr.colors[s->p.i("line_color")].name : QString("color");
    s->p.set("fill_color", pr.addColor(light, base + "-light"));
}

void distinctColors(Project &pr, const QVector<QPair<int, int>> &sets)
{
    static const int cycle[] = {4, 2, 3, 11, 8, 9, 10, 6, 12, 14, 13, 15, 1, 5, 7};
    int k = 0;
    for (const auto &gs : sets) {
        Graph *g = pr.graph(gs.first);
        DataSet *s = g ? g->set(gs.second) : nullptr;
        if (!s)
            continue;
        setSetColor(*s, cycle[k++ % 15]);
    }
}

}  // namespace ng

namespace ng {

QVector<int> layerOrder(const QVector<int> &order, const QVector<int> &selected, int move)
{
    QVector<int> out = order;
    auto sel = [&](int v) { return selected.contains(v); };
    if (move == LayerFront || move == LayerBack) {
        QVector<int> a, b;
        for (int v : order)
            (sel(v) ? a : b) << v;
        return move == LayerFront ? b + a : a + b;
    }
    if (move == LayerForward) {
        for (int i = out.size() - 2; i >= 0; --i)
            if (sel(out[i]) && !sel(out[i + 1]))
                std::swap(out[i], out[i + 1]);
    } else {
        for (int i = 1; i < out.size(); ++i)
            if (sel(out[i]) && !sel(out[i - 1]))
                std::swap(out[i], out[i - 1]);
    }
    return out;
}

QHash<int, int> reorderSets(Graph &g, const QVector<int> &order)
{
    QHash<int, int> map;
    QVector<int> ids;
    for (const DataSet &s : g.sets)
        ids << s.id;
    std::sort(ids.begin(), ids.end());
    QVector<DataSet> ns;
    for (int k = 0; k < order.size(); ++k)
        if (const DataSet *s = g.set(order[k])) {
            DataSet c = *s;
            map[c.id] = ids.value(ns.size(), c.id);
            c.id = map[s->id];
            ns << c;
        }
    // наборы, не попавшие в order, — в конец
    for (const DataSet &s : g.sets)
        if (!order.contains(s.id)) {
            DataSet c = s;
            map[c.id] = ids.value(ns.size(), c.id);
            c.id = map[s.id];
            ns << c;
        }
    g.sets = ns;
    return map;
}

QHash<int, int> reorderGraphs(Project &pr, const QVector<int> &order)
{
    QHash<int, int> map;
    QVector<int> ids;
    for (const Graph &g : pr.graphs)
        ids << g.id;
    std::sort(ids.begin(), ids.end());
    QVector<Graph> ng;
    auto add = [&](const Graph &g) {
        Graph c = g;
        map[g.id] = ids.value(ng.size(), g.id);
        c.id = map[g.id];
        ng << c;
    };
    for (int id : order)
        if (const Graph *g = pr.graph(id))
            add(*g);
    for (const Graph &g : pr.graphs)
        if (!order.contains(g.id))
            add(g);
    pr.graphs = ng;
    for (DrawObject &o : pr.objects)
        if (map.contains(o.graph))
            o.graph = map[o.graph];
    return map;
}

QHash<QPair<int, int>, int> moveSetsToGraph(Project &pr, const QVector<QPair<int, int>> &sets, int target, int pos)
{
    QHash<QPair<int, int>, int> out;
    Graph *t = pr.graph(target);
    if (!t)
        return out;
    // порядок целевого графика: старые наборы без переносимых, переносимые — в позицию pos
    QVector<DataSet> moving;
    for (const auto &gs : sets)
        if (Graph *g = pr.graph(gs.first))
            if (const DataSet *d = g->set(gs.second))
                moving << *d;
    for (const auto &gs : sets)
        if (Graph *g = pr.graph(gs.first))
            g->sets.erase(std::remove_if(g->sets.begin(), g->sets.end(),
                                         [&](const DataSet &d) { return d.id == gs.second; }),
                          g->sets.end());
    t = pr.graph(target);
    QVector<int> ids;
    for (const DataSet &d : t->sets)
        ids << d.id;
    int next = t->nextSetId();
    for (int k = 0; k < moving.size(); ++k)
        ids << next + k;
    std::sort(ids.begin(), ids.end());
    QVector<DataSet> order = t->sets;
    if (pos < 0 || pos > order.size())
        pos = order.size();
    for (int k = 0; k < moving.size(); ++k)
        order.insert(pos + k, moving[k]);
    for (int k = 0; k < order.size(); ++k) {
        const int movedIdx = k - pos;
        const int nid = ids[k];
        if (movedIdx >= 0 && movedIdx < moving.size())
            out[sets[movedIdx]] = nid;
        order[k].id = nid;
    }
    t->sets = order;
    return out;
}

}  // namespace ng

namespace ng {

TextData parseDataText(const QString &text)
{
    TextData d;
    QVector<QVector<double>> rows;
    auto flush = [&]() {
        if (rows.isEmpty())
            return;
        int n = 0;
        for (const auto &r : rows)
            n = qMax(n, int(r.size()));
        Table t;
        t.cols = QVector<QVector<double>>(n);
        for (const auto &r : rows)
            for (int k = 0; k < n; ++k)
                t.cols[k] << (k < r.size() ? r[k] : std::numeric_limits<double>::quiet_NaN());
        d.points += t.rows();
        d.maxCols = qMax(d.maxCols, n);
        d.blocks << t;
        rows.clear();
    };
    const QStringList lines = text.split('\n');
    for (int i = 0; i < lines.size(); ++i) {
        const QString s = lines[i].trimmed();
        if (s.isEmpty() || s.startsWith('&')) {
            flush();
            continue;
        }
        if (s.startsWith('#') || s.startsWith('@'))
            continue;
        static const QRegularExpression fre(QStringLiteral("^(?:y|f\\s*\\(\\s*x\\s*\\))\\s*=\\s*(.+)$"),
                                            QRegularExpression::CaseInsensitiveOption);
        const auto fm = fre.match(s);
        if (fm.hasMatch()) {  // функция: y = sin(x)
            flush();
            const QString e = fm.captured(1).trimmed();
            QString err;
            QVector<double> probe;
            if (evalOnArray(e, {1.0}, probe, &err))
                d.formulas << e;
            else
                d.errors << T("строка %1: %2").arg(i + 1).arg(err);
            continue;
        }
        QVector<double> r;
        if (!parseNumberLine(QStringView(s), r)) {
            d.errors << T("строка %1: «%2» — не числа").arg(i + 1).arg(s.left(40));
            continue;
        }
        rows << r;
    }
    flush();
    return d;
}

QVector<int> addFormulaSets(Graph &g, const QStringList &formulas, double x1, double x2, int n,
                            const QString &legend)
{
    QVector<int> out;
    for (const QString &f : formulas) {
        QString err;
        const int id = createFromFormula(g, x1, x2, n, f, &err);
        if (id < 0)
            continue;
        if (!legend.isEmpty())
            g.set(id)->p.set("legend", formulas.size() > 1 ? QString("%1 %2").arg(legend).arg(out.size() + 1) : legend);
        out << id;
    }
    return out;
}

static void fillSetFromTable(DataSet &s, const Table &t, const QString &type)
{
    const int nc = t.cols.size();
    QString ty = type;
    if (ty == "auto" || ty.isEmpty())
        ty = "xy";
    s.setType(ty);
    const int need = setTypeColumns(ty);
    QVector<QVector<double>> cols;
    if (nc == 1) {  // только Y — X по номеру точки
        QVector<double> idx(t.rows());
        std::iota(idx.begin(), idx.end(), 0.0);
        cols << idx << t.cols[0];
    } else {
        for (int k = 0; k < nc; ++k)
            cols << t.cols[k];
    }
    while (cols.size() < need)
        cols << QVector<double>(t.rows(), 0.0);
    cols.resize(need);
    s.cols = cols;
    s.strings.clear();
}

QVector<int> addSetsFromText(Graph &g, const TextData &d, const QString &type, const QString &legend)
{
    QVector<int> out;
    for (const Table &t : d.blocks) {
        if (t.rows() == 0)
            continue;
        if ((type == "auto" || type.isEmpty()) && t.cols.size() > 2) {
            // X и несколько Y
            for (int k = 1; k < t.cols.size(); ++k) {
                DataSet &s = g.newSet("xy");
                s.cols = {t.cols[0], t.cols[k]};
                out << s.id;
            }
            continue;
        }
        DataSet &s = g.newSet("xy");
        fillSetFromTable(s, t, type);
        out << s.id;
    }
    for (int k = 0; k < out.size(); ++k) {
        DataSet *s = g.set(out[k]);
        if (!legend.isEmpty())
            s->p.set("legend", out.size() > 1 ? QString("%1 %2").arg(legend).arg(k + 1) : legend);
        if (s->size() == 1 || s->type() != "xy") {
            // одиночные точки и погрешности видны лучше с символами
            s->p.set("symbol", 1);
            s->p.set("symbol_size", 0.7);
            s->p.set("symbol_fill_pattern", 1);
        }
        if (s->type().startsWith("xyd") || s->type() == "xyz" || s->type() == "xysize")
            s->p.set("line_type", 0);
    }
    return out;
}

bool replaceSetFromText(DataSet &s, const TextData &d, const QString &type)
{
    if (d.blocks.isEmpty())
        return false;
    fillSetFromTable(s, d.blocks.first(), (type == "auto" || type.isEmpty()) ? s.type() : type);
    return true;
}

QString setToText(const DataSet &s)
{
    QString out;
    const int n = s.size();
    for (int i = 0; i < n; ++i) {
        QStringList row;
        for (const auto &c : s.cols)
            row << QString::number(c.value(i), 'g', 12);
        out += row.join(' ') + '\n';
    }
    return out;
}

void ensureVisible(Graph &g, const QVector<int> &sets)
{
    bool others = false;
    for (const DataSet &s : g.sets)
        if (!sets.contains(s.id) && !s.hidden() && s.size() > 0)
            others = true;
    if (!others) {  // пустой график — просто подобрать масштаб
        autoscaleGraph(g);
        return;
    }
    const Vec4 w = g.p.v4("world");
    const double x1 = qMin(w[0], w[2]), x2 = qMax(w[0], w[2]), y1 = qMin(w[1], w[3]), y2 = qMax(w[1], w[3]);
    for (int sid : sets) {
        const DataSet *s = g.set(sid);
        double a, b, c, dd;
        if (!s || !setBounds(*s, &g, a, b, c, dd))
            continue;
        if (a < x1 || b > x2 || c < y1 || dd > y2) {
            autoscaleGraph(g);  // не помещается — масштаб по всем наборам
            return;
        }
    }
}

}  // namespace ng
