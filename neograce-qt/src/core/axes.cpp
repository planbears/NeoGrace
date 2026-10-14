#include "axes.h"

#include "expr.h"

#include <cmath>
#include <limits>

namespace ng {

Scale scaleOf(const QString &s)
{
    if (s == "Logarithmic")
        return Scale::Log;
    if (s == "Reciprocal")
        return Scale::Reciprocal;
    if (s == "Logit")
        return Scale::Logit;
    return Scale::Normal;
}

double fscale(double w, Scale s)
{
    switch (s) {
    case Scale::Log: return w > 0 ? std::log10(w) : std::numeric_limits<double>::quiet_NaN();
    case Scale::Reciprocal: return w != 0 ? 1.0 / w : std::numeric_limits<double>::quiet_NaN();
    case Scale::Logit:
        return (w > 0 && w < 1) ? std::log(w / (1 - w)) : std::numeric_limits<double>::quiet_NaN();
    default: return w;
    }
}

double ifscale(double w, Scale s)
{
    switch (s) {
    case Scale::Log: return std::pow(10.0, w);
    case Scale::Reciprocal: return w != 0 ? 1.0 / w : std::numeric_limits<double>::quiet_NaN();
    case Scale::Logit: return std::exp(w) / (1 + std::exp(w));
    default: return w;
    }
}

WorldMap::WorldMap(const Graph &g)
{
    Vec4 v = g.p.v4("view");
    vx1 = qMin(v[0], v[2]);
    vx2 = qMax(v[0], v[2]);
    vy1 = qMin(v[1], v[3]);
    vy2 = qMax(v[1], v[3]);
    Vec4 w = g.p.v4("world");
    wx1 = w[0];
    wy1 = w[1];
    wx2 = w[2];
    wy2 = w[3];
    xs = scaleOf(g.p.s("xscale"));
    ys = scaleOf(g.p.s("yscale"));
    xinv = g.p.b("xinvert");
    yinv = g.p.b("yinvert");
    fx1 = fscale(wx1, xs);
    fx2 = fscale(wx2, xs);
    fy1 = fscale(wy1, ys);
    fy2 = fscale(wy2, ys);
    ok = std::isfinite(fx1) && std::isfinite(fx2) && std::isfinite(fy1) && std::isfinite(fy2)
         && fx1 != fx2 && fy1 != fy2 && vx2 > vx1 && vy2 > vy1;
    if (!ok) {
        // «Починим» вырожденный мир, чтобы хотя бы что-то нарисовать
        if (!std::isfinite(fx1) || !std::isfinite(fx2) || fx1 == fx2) {
            if (xs == Scale::Log) {
                fx1 = -1;
                fx2 = 1;
            } else {
                double c = std::isfinite(fx1) ? fx1 : 0;
                fx1 = c - 0.5;
                fx2 = c + 0.5;
            }
        }
        if (!std::isfinite(fy1) || !std::isfinite(fy2) || fy1 == fy2) {
            if (ys == Scale::Log) {
                fy1 = -1;
                fy2 = 1;
            } else {
                double c = std::isfinite(fy1) ? fy1 : 0;
                fy1 = c - 0.5;
                fy2 = c + 0.5;
            }
        }
        if (vx2 <= vx1)
            vx2 = vx1 + 0.01;
        if (vy2 <= vy1)
            vy2 = vy1 + 0.01;
    }
}

double WorldMap::xv(double x) const
{
    double t = (fscale(x, xs) - fx1) / (fx2 - fx1);
    if (xinv)
        t = 1 - t;
    return vx1 + t * (vx2 - vx1);
}

double WorldMap::yv(double y) const
{
    double t = (fscale(y, ys) - fy1) / (fy2 - fy1);
    if (yinv)
        t = 1 - t;
    return vy1 + t * (vy2 - vy1);
}

double WorldMap::xw(double vx) const
{
    double t = (vx - vx1) / (vx2 - vx1);
    if (xinv)
        t = 1 - t;
    return ifscale(fx1 + t * (fx2 - fx1), xs);
}

double WorldMap::yw(double vy) const
{
    double t = (vy - vy1) / (vy2 - vy1);
    if (yinv)
        t = 1 - t;
    return ifscale(fy1 + t * (fy2 - fy1), ys);
}

bool WorldMap::validX(double x) const { return std::isfinite(fscale(x, xs)); }
bool WorldMap::validY(double y) const { return std::isfinite(fscale(y, ys)); }

// ---------------------------------------------------------------------------

static void jdToCal(double jd, int &y, int &m, int &d, int &hh, int &mm, int &ss)
{
    // Алгоритм Флиганда; jd — юлианский день
    double j = jd + 0.5;
    long Z = long(std::floor(j));
    double F = j - Z;
    long A = Z;
    if (Z >= 2299161) {
        long alpha = long((Z - 1867216.25) / 36524.25);
        A = Z + 1 + alpha - alpha / 4;
    }
    long B = A + 1524;
    long C = long((B - 122.1) / 365.25);
    long D = long(365.25 * C);
    long E = long((B - D) / 30.6001);
    double day = B - D - long(30.6001 * E) + F;
    d = int(day);
    m = E < 14 ? int(E - 1) : int(E - 13);
    y = m > 2 ? int(C - 4716) : int(C - 4715);
    double secs = (day - d) * 86400.0;
    int s = int(std::lround(secs));
    hh = s / 3600;
    mm = (s % 3600) / 60;
    ss = s % 60;
}

QString formatNumber(double v, const QString &fmt, int prec)
{
    prec = qBound(0, prec, 15);
    if (!std::isfinite(v))
        return std::isnan(v) ? QStringLiteral("NaN") : (v > 0 ? QStringLiteral("inf") : QStringLiteral("-inf"));
    static const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    static const char *monthl[] = {"January", "February", "March", "April", "May", "June", "July",
                                   "August", "September", "October", "November", "December"};
    static const char *dows[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
    static const char *dowl[] = {"Monday", "Tuesday", "Wednesday", "Thursday", "Friday",
                                 "Saturday", "Sunday"};
    if (fmt == "decimal") {
        QString s = QString::asprintf("%.*f", prec, v);
        if (s.toDouble() == 0.0)
            s = QString::asprintf("%.*f", prec, 0.0);
        return s;
    }
    if (fmt == "exponential") {
        QString s = QString::asprintf("%.*e", prec, v);
        if (s.toDouble() == 0.0)
            s = QString::asprintf("%.*e", prec, 0.0);
        return s;
    }
    if (fmt == "scientific") {
        if (v == 0)
            return QString::asprintf("%.*f", prec, 0.0);
        int e = int(std::floor(std::log10(std::fabs(v))));
        double m = v / std::pow(10.0, e);
        return QString::asprintf("%.*f\\x\\#{b4}\\f{}10\\S%d\\N", prec, m, e);
    }
    if (fmt == "power") {
        if (v == 0)
            return QString::asprintf("%.*f", prec, 0.0);
        if (v < 0)
            return QString::asprintf("-10\\S%.*f\\N", prec, std::log10(-v));
        return QString::asprintf("10\\S%.*f\\N", prec, std::log10(v));
    }
    if (fmt == "engineering") {
        static const char *pre[] = {"y", "z", "a", "f", "p", "n", "µ", "m", "", "k", "M", "G", "T", "P", "E", "Z", "Y"};
        int e = 0;
        if (v != 0) {
            e = int(std::floor(std::log10(std::fabs(v))));
            e = qBound(-24, int(std::floor(e / 3.0)) * 3, 24);
        }
        QString s = QString::asprintf("%.*f", prec, v / std::pow(10.0, e));
        QString p = QString::fromUtf8(pre[(e + 24) / 3]);
        return p.isEmpty() ? s : s + ' ' + p;
    }
    if (fmt == "computing") {
        int e = 0;
        if (v != 0) {
            e = int(std::floor(std::log2(std::fabs(v))));
            e = e < 10 ? 0 : e > 80 ? 80 : (e / 10) * 10;
        }
        QString s = QString::asprintf("%.*g", prec, v / std::pow(2.0, e));
        if (e < 80 && s == "1024") {
            e += 10;
            s = QString::asprintf("%.*g", prec, v / std::pow(2.0, e));
        }
        static const char *cp[] = {"", "K", "M", "G", "T", "P", "E", "Z", "Y"};
        return s + cp[e / 10];
    }
    if (fmt.startsWith("degrees") || fmt.startsWith("mmss")) {
        const bool lat = fmt.endsWith("lat");
        QString hemi = lat ? (v > 0 ? "N" : v < 0 ? "S" : "") : (v > 0 ? "E" : v < 0 ? "W" : "");
        double a = std::fabs(v);
        if (fmt.startsWith("degreesmmss")) {
            int dg = int(a);
            double r = (a - dg) * 60;
            int mn = int(r);
            double sc = (r - mn) * 60;
            return QString::asprintf("%d°%02d'%0*.*f\"", dg, mn, prec > 0 ? prec + 3 : 2, prec, sc) + hemi;
        }
        if (fmt.startsWith("degreesmm")) {
            int dg = int(a);
            double r = (a - dg) * 60;
            return QString::asprintf("%d°%0*.*f'", dg, prec > 0 ? prec + 3 : 2, prec, r) + hemi;
        }
        if (fmt.startsWith("mmss")) {
            int dg = int(a);
            double r = (a - dg) * 60;
            int mn = int(r);
            double sc = (r - mn) * 60;
            return QString::asprintf("%02d'%0*.*f\"", mn, prec > 0 ? prec + 3 : 2, prec, sc) + hemi;
        }
        return QString::asprintf("%.*f°", prec, a) + hemi;
    }
    // даты (юлианские дни)
    static const QStringList dateFmts = {"ddmmyy", "mmddyy", "yymmdd", "mmyy", "mmdd", "monthday",
                                         "daymonth", "months", "monthsy", "monthl", "dayofweek",
                                         "dayofweekl", "dayofyear", "hms", "mmddhms", "mmddyyhms",
                                         "yymmddhms"};
    if (dateFmts.contains(fmt)) {
        int y, m, d, hh, mm, ss;
        jdToCal(v, y, m, d, hh, mm, ss);
        const int mi = qBound(0, m - 1, 11);
        if (fmt == "ddmmyy") return QString::asprintf("%02d-%02d-%04d", d, m, y);
        if (fmt == "mmddyy") return QString::asprintf("%02d-%02d-%04d", m, d, y);
        if (fmt == "yymmdd") return QString::asprintf("%04d-%02d-%02d", y, m, d);
        if (fmt == "mmyy") return QString::asprintf("%02d-%04d", m, y);
        if (fmt == "mmdd") return QString::asprintf("%02d-%02d", m, d);
        if (fmt == "monthday") return QString::asprintf("%s-%02d", months[mi], d);
        if (fmt == "daymonth") return QString::asprintf("%02d-%s", d, months[mi]);
        if (fmt == "months") return months[mi];
        if (fmt == "monthsy") return QString::asprintf("%s-%04d", months[mi], y);
        if (fmt == "monthl") return monthl[mi];
        long jdn = long(std::floor(v + 0.5));
        const int dw = int(((jdn % 7) + 7) % 7);  // 0 = понедельник
        if (fmt == "dayofweek") return dows[dw];
        if (fmt == "dayofweekl") return dowl[dw];
        if (fmt == "dayofyear") {
            static const int cum[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
            bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
            return QString::number(cum[mi] + d + (leap && m > 2 ? 1 : 0));
        }
        if (fmt == "hms") return QString::asprintf("%02d:%02d:%02d", hh, mm, ss);
        if (fmt == "mmddhms") return QString::asprintf("%02d-%02d %02d:%02d:%02d", m, d, hh, mm, ss);
        if (fmt == "mmddyyhms") return QString::asprintf("%02d-%02d-%04d %02d:%02d:%02d", m, d, y, hh, mm, ss);
        return QString::asprintf("%04d-%02d-%02d %02d:%02d:%02d", y, m, d, hh, mm, ss);
    }
    // general
    QString s = QString::asprintf("%.*g", prec, v);
    if (s.toDouble() == 0.0)
        s = "0";
    return s;
}

double niceStep(double span, int nticks)
{
    span = std::fabs(span);
    if (!(span > 0) || !std::isfinite(span))
        return 1.0;
    double raw = span / qMax(1, nticks);
    double mag = std::pow(10.0, std::floor(std::log10(raw)));
    double f = raw / mag;
    double nf = f < 1.5 ? 1 : f < 2.25 ? 2 : f < 3.5 ? 2.5 : f < 7.5 ? 5 : 10;
    return nf * mag;
}

void autoTickAxis(Graph &g, int ax)
{
    Axis &a = g.axes[ax];
    const bool isX = (ax % 2) == 0;
    Vec4 w = g.p.v4("world");
    const Scale sc = scaleOf(g.p.s(isX ? "xscale" : "yscale"));
    double lo = isX ? w[0] : w[1], hi = isX ? w[2] : w[3];
    if (sc == Scale::Log) {
        a.p.set("tick_major", 10.0);
        a.p.set("tick_minor_ticks", 8);
        double dec = std::fabs(fscale(hi, sc) - fscale(lo, sc));
        if (std::isfinite(dec) && dec > 12) {
            double step = std::pow(10.0, std::ceil(dec / 8));
            a.p.set("tick_major", step);
            a.p.set("tick_minor_ticks", 0);
        }
        a.p.set("ticklabel_format", a.p.s("ticklabel_format") == "general" ? QString("power") : a.p.s("ticklabel_format"));
        if (a.p.s("ticklabel_format") == "power")
            a.p.set("ticklabel_prec", 0);
        return;
    }
    const int n = qMax(2, a.p.i("tick_default"));
    const double step = niceStep(hi - lo, n);
    a.p.set("tick_major", step);
    const double f = step / std::pow(10.0, std::floor(std::log10(step)));
    a.p.set("tick_minor_ticks", (std::fabs(f - 2) < 1e-9 || std::fabs(f - 2.5) < 1e-9) ? 1 : 4);
    if (qAbs(f - 5) < 1e-9)
        a.p.set("tick_minor_ticks", 4);
}

QVector<Tick> computeTicks(const Graph &g, int ax)
{
    const Axis &a = g.axes[ax];
    QVector<Tick> out;
    const bool isX = (ax % 2) == 0;
    Vec4 w = g.p.v4("world");
    double lo = isX ? w[0] : w[1], hi = isX ? w[2] : w[3];
    if (lo > hi)
        std::swap(lo, hi);
    const Scale sc = scaleOf(g.p.s(isX ? "xscale" : "yscale"));
    const QString spec = a.p.s("tick_spec_type");

    if (spec != "none" && !a.spec.isEmpty()) {
        for (const auto &t : a.spec)
            out.append({t.value, t.major, spec == "both" && t.major ? t.label : QString()});
        if (spec == "both")
            return out;
    } else {
        double slo = lo, shi = hi, step = a.p.d("tick_major");
        if (sc == Scale::Log) {
            slo = fscale(lo, sc);
            shi = fscale(hi, sc);
            step = fscale(step, sc);
        }
        int nminor = qMax(0, a.p.i("tick_minor_ticks"));
        if (!(step > 0) || !std::isfinite(step) || !std::isfinite(slo) || !std::isfinite(shi)
            || (shi - slo) / step > 1000) {
            // как в Grace: автоматический шаг
            if (sc == Scale::Log) {
                slo = std::isfinite(slo) ? slo : -1;
                shi = std::isfinite(shi) ? shi : 1;
                step = qMax(1.0, std::ceil((shi - slo) / 8));
            } else {
                step = niceStep(shi - slo, qMax(2, a.p.i("tick_default")));
            }
        }
        if (a.p.b("tick_rounded"))
            slo = std::floor(slo / step) * step;
        const int nmajor = int(std::ceil((shi - slo) / step + 1));
        if (nmajor > 2000)
            return out;
        if (sc == Scale::Log && nminor > 0 && std::fabs(step - 1.0) > 1e-9)
            nminor = 0;  // десятичные доли не для шага 100, 1000…
        for (int k = 0; k < nmajor; ++k) {
            double wt;
            if (sc == Scale::Log) {
                wt = ifscale(slo + k * step, sc);
            } else {
                wt = slo + k * step;
                if (a.p.b("tick_rounded") && std::fabs(wt) < 1e-6 * step)
                    wt = 0.0;
            }
            out.append({wt, true, QString()});
            if (k == nmajor - 1)
                break;
            for (int j = 0; j < nminor; ++j) {
                double mw = sc == Scale::Log ? wt * (j + 2) : wt + (j + 1) * step / (nminor + 1);
                if (sc == Scale::Log && mw >= ifscale(slo + (k + 1) * step, sc) * (1 - 1e-12))
                    break;
                out.append({mw, false, QString()});
            }
        }
    }

    // подписи
    const QString fmt = a.p.s("ticklabel_format");
    const int prec = a.p.i("ticklabel_prec");
    const QString formula = a.p.s("ticklabel_formula");
    Expr e;
    const bool useFormula = !formula.trimmed().isEmpty() && e.parse(formula);
    for (auto &t : out) {
        if (!t.major)
            continue;
        double v = t.w;
        if (useFormula) {
            Expr::Ctx c;
            c.t = t.w;
            v = e.eval(c);
        }
        t.label = formatNumber(v, fmt, prec);
    }
    return out;
}

}  // namespace ng
