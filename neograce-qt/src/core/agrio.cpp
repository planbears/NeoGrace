#include "agrio.h"
#include "i18n.h"

#include "axes.h"
#include "gracetext.h"
#include "ops.h"
#include "render.h"

#include <algorithm>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStringConverter>
#include <QTextStream>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace ng {

static const double NaN = std::numeric_limits<double>::quiet_NaN();

QString normalizeCommand(const QString &cmd)
{
    QString out;
    out.reserve(cmd.size());
    bool inq = false, prevSpace = false;
    const int n = cmd.size();
    for (int i = 0; i < n; ++i) {
        QChar c = cmd[i];
        if (inq) {
            out += c;
            if (c == '\\' && i + 1 < n) {
                out += cmd[i + 1];
                ++i;
                continue;
            }
            if (c == '"')
                inq = false;
        } else if (c == '"') {
            inq = true;
            out += c;
            prevSpace = false;
        } else if (c == ' ' || c == '\t' || c == '\r') {
            if (!prevSpace)
                out += ' ';
            prevSpace = true;
        } else {
            out += c.toLower();
            prevSpace = false;
        }
    }
    return out.trimmed();
}

// -----------------------------------------------------------------------------

namespace {

struct Row {
    QVector<double> v;
    QString str;
    bool hasStr = false;
};

void storeRows(DataSet &s, const QVector<Row> &rows)
{
    const int n = setTypeColumns(s.type());
    s.cols = QVector<QVector<double>>(n);
    s.strings.clear();
    for (auto &c : s.cols)
        c.reserve(rows.size());
    bool anyStr = false;
    for (const Row &r : rows) {
        for (int k = 0; k < n; ++k)
            s.cols[k].append(k < r.v.size() ? r.v[k] : NaN);
        anyStr |= r.hasStr;
    }
    if (s.type() == "xystring" || anyStr) {
        for (const Row &r : rows)
            s.strings << r.str;
    }
}

const QRegularExpression &re(const char *pat)
{
    // кэш скомпилированных выражений по адресу литерала
    static std::unordered_map<const char *, QRegularExpression> cache;
    auto it = cache.find(pat);
    if (it == cache.end())
        it = cache.emplace(pat, QRegularExpression(QString::fromLatin1(pat))).first;
    return it->second;
}

// Быстрый разбор строки чисел (миллионы строк): разделители — пробелы,
// табуляция, запятая, «;».  Не зависит от локали.
bool fastDouble(const QChar *p, int n, double &out)
{
    static const double pow10[] = {1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11,
                                   1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};
    int i = 0;
    bool neg = false;
    if (i < n && (p[i] == '-' || p[i] == '+')) {
        neg = p[i] == '-';
        ++i;
    }
    quint64 mant = 0;
    int digits = 0, exp10 = 0;
    bool any = false;
    while (i < n && p[i].unicode() >= '0' && p[i].unicode() <= '9') {
        any = true;
        if (digits < 19) {
            mant = mant * 10 + (p[i].unicode() - '0');
            if (mant) ++digits;
        } else {
            ++exp10;
        }
        ++i;
    }
    if (i < n && p[i] == '.') {
        ++i;
        while (i < n && p[i].unicode() >= '0' && p[i].unicode() <= '9') {
            any = true;
            if (digits < 19) {
                mant = mant * 10 + (p[i].unicode() - '0');
                if (mant) ++digits;
                --exp10;
            }
            ++i;
        }
    }
    if (!any)
        return false;
    if (i < n && (p[i] == 'e' || p[i] == 'E' || p[i] == 'd' || p[i] == 'D')) {
        ++i;
        bool eneg = false;
        if (i < n && (p[i] == '-' || p[i] == '+')) {
            eneg = p[i] == '-';
            ++i;
        }
        int e = 0;
        bool edig = false;
        while (i < n && p[i].unicode() >= '0' && p[i].unicode() <= '9') {
            if (e < 10000) e = e * 10 + (p[i].unicode() - '0');
            edig = true;
            ++i;
        }
        if (!edig)
            return false;
        exp10 += eneg ? -e : e;
    }
    if (i != n)
        return false;
    double v;
    if (digits <= 15 && exp10 >= -22 && exp10 <= 22) {
        v = double(mant);  // точный быстрый путь
        v = exp10 < 0 ? v / pow10[-exp10] : v * pow10[exp10];
    } else {
        // редкий случай — точное преобразование через Qt
        QString t = QString(p, n);
        t.replace('d', 'e').replace('D', 'e');
        bool ok = false;
        v = QLocale::c().toDouble(t, &ok);
        if (!ok)
            return false;
        out = v;
        return true;
    }
    out = neg ? -v : v;
    return true;
}

bool parseNumbers(QStringView s, QVector<double> &out)
{
    const QChar *p = s.data();
    const int n = int(s.size());
    int i = 0;
    while (i < n) {
        while (i < n && (p[i].isSpace() || p[i] == ',' || p[i] == ';'))
            ++i;
        if (i >= n)
            break;
        int j = i;
        while (j < n && !p[j].isSpace() && p[j] != ',' && p[j] != ';')
            ++j;
        double d;
        if (!fastDouble(p + i, j - i, d)) {
            const QString tok = QString(p + i, j - i).toLower();
            if (tok == "nan" || tok == "-nan")
                d = NaN;
            else if (tok == "inf" || tok == "+inf")
                d = std::numeric_limits<double>::infinity();
            else if (tok == "-inf")
                d = -std::numeric_limits<double>::infinity();
            else
                return false;
        }
        out << d;
        i = j;
    }
    return true;
}

QString placement(const QString &w)
{
    if (w == "top" || w == "right" || w == "opposite")
        return "opposite";
    if (w == "both")
        return "both";
    return "normal";
}

class Parser {
public:
    explicit Parser(bool keep) : keepExtras(keep)
    {
        pr.fonts.clear();
    }

    Project pr;
    bool keepExtras;
    int curGraph = -1;
    int curObj = -1;
    int tgtGraph = -1, tgtSet = -1;
    QVector<Row> tgtRows;
    bool blockActive = false;
    int blockGraph = 0;
    QString blockType;
    QVector<Row> blockRows;
    QString pendingType;
    QString curLine;
    int fileVersion = 50125;  // без @version считаем файл современным
    bool fontsMapped = false;
    QHash<QString, QString> fillObs;  // «fill with color|pattern» старых версий
    bool pageSizeGiven = false;

    Graph &graph()
    {
        if (curGraph < 0)
            curGraph = 0;
        return pr.graphOrCreate(curGraph);
    }

    void extra(bool graphLevel)
    {
        if (!keepExtras)
            return;
        if (graphLevel && curGraph >= 0)
            graph().extra << curLine;
        else
            pr.headerExtra << curLine;
    }

    void flushTarget()
    {
        if (tgtGraph >= 0) {
            if (auto *g = pr.graph(tgtGraph))
                if (auto *s = g->set(tgtSet))
                    storeRows(*s, tgtRows);
        }
        tgtGraph = tgtSet = -1;
        tgtRows.clear();
    }

    void flushBlock()
    {
        if (!blockActive)
            return;
        blockActive = false;
        if (blockRows.isEmpty())
            return;
        Graph &g = pr.graphOrCreate(blockGraph);
        int ncols = 0;
        for (const Row &r : blockRows)
            ncols = qMax(ncols, int(r.v.size()));
        // наборы, уже объявленные (например, легенды в xvg), но пустые
        QVector<int> freeIds;
        for (const auto &s : g.sets)
            if (s.size() == 0)
                freeIds << s.id;
        auto takeSet = [&]() -> DataSet & {
            if (!freeIds.isEmpty())
                return *g.set(freeIds.takeFirst());
            return g.newSet();
        };
        const QString st = blockType;
        if ((st.isEmpty() || st == "xy" || st == "nxy") && ncols > 2) {
            for (int k = 1; k < ncols; ++k) {
                DataSet &s = takeSet();
                const int sid = s.id;
                s.setType("xy");
                QVector<Row> rows;
                rows.reserve(blockRows.size());
                for (const Row &r : blockRows) {
                    Row x;
                    x.v = {r.v.value(0, NaN), k < r.v.size() ? r.v[k] : NaN};
                    rows << x;
                }
                storeRows(*g.set(sid), rows);
            }
        } else {
            DataSet &s = takeSet();
            s.p.set("type", kSetTypes.contains(st) ? st : QString("xy"));
            storeRows(s, blockRows);
        }
        blockRows.clear();
    }

    void dataLine(const QString &line)
    {
        Row row;
        QString sline = line;
        int q = line.indexOf('"');
        if (q >= 0) {
            row.str = unquoteString(line.mid(q));
            row.hasStr = true;
            sline = line.left(q);
        }
        if (!parseNumbers(QStringView(sline), row.v)) {
            if (pr.warnings.size() < 50)
                pr.warnings << T("Непонятная строка данных: ") + line.left(60);
            return;
        }
        if (row.v.isEmpty())
            return;
        if (tgtGraph >= 0) {
            tgtRows << row;
        } else {
            if (!blockActive) {
                blockActive = true;
                blockGraph = curGraph < 0 ? 0 : curGraph;
                blockType = pendingType;
                blockRows.clear();
            }
            blockRows << row;
        }
    }

    bool graphCmd(Graph &g, const QString &cmd)
    {
        QRegularExpressionMatch m;
        if ((m = re("^(world|view) (xmin|xmax|ymin|ymax) (\\S+)$").match(cmd)).hasMatch()) {
            const QString key = m.captured(1);
            Vec4 v = g.p.v4(key);
            static const QHash<QString, int> idx = {{"xmin", 0}, {"ymin", 1}, {"xmax", 2}, {"ymax", 3}};
            bool ok = false;
            double d = m.captured(3).toDouble(&ok);
            if (!ok)
                return false;
            v[idx.value(m.captured(2))] = d;
            g.p.setV4(key, v[0], v[1], v[2], v[3]);
            return true;
        }
        if ((m = re("^legend (x1|y1) (\\S+)$").match(cmd)).hasMatch()) {
            Vec2 pos = g.p.v2("legend_pos");
            double d = m.captured(2).toDouble();
            if (m.captured(1) == "x1")
                pos[0] = d;
            else
                pos[1] = d;
            g.p.setV2("legend_pos", pos[0], pos[1]);
            return true;
        }
        if ((m = re("^legend string (\\d+) (\".*\")$").match(cmd)).hasMatch()) {
            g.setOrCreate(m.captured(1).toInt()).p.set("legend", unquoteString(m.captured(2)));
            return true;
        }
        if (cmd.startsWith("stack world")) {
            if (keepExtras)
                g.extra << curLine;
            return true;
        }
        if ((m = re("^legend box (on|off)$").match(cmd)).hasMatch()) {
            if (m.captured(1) == "off" && fileVersion <= 40102)
                g.p.set("legend_box_pattern", 0);
            return true;
        }
        return applySchema(graphSchema(), g.p, cmd);
    }

    void axisCmd(Axis &ax, const QString &rest)
    {
        QRegularExpressionMatch m;
        auto ensure = [&](int n) {
            while (ax.spec.size() < n) {
                TickSpec t;
                t.hasValue = false;
                ax.spec.append(t);
            }
        };
        if ((m = re("^tick (major|minor) (\\d+)\\s*,\\s*(\\S+)$").match(rest)).hasMatch()) {
            int i = m.captured(2).toInt();
            ensure(i + 1);
            ax.spec[i].major = m.captured(1) == "major";
            ax.spec[i].value = m.captured(3).toDouble();
            ax.spec[i].hasValue = true;
            return;
        }
        if ((m = re("^ticklabel (\\d+)\\s*,\\s*(\".*\")$").match(rest)).hasMatch()) {
            int i = m.captured(1).toInt();
            ensure(i + 1);
            ax.spec[i].label = unquoteString(m.captured(2));
            return;
        }
        if ((m = re("^tick spec (\\d+)$").match(rest)).hasMatch()) {
            ensure(m.captured(1).toInt());
            return;
        }
        if ((m = re("^(label|ticklabel|tick) op (\\w+)$").match(rest)).hasMatch()) {
            const QString w = m.captured(1);
            const QString key = w == "label" ? "label_side" : w == "ticklabel" ? "ticklabel_place" : "tick_place";
            ax.p.set(key, placement(m.captured(2)));
            return;
        }
        if ((m = re("^tick type auto$").match(rest)).hasMatch()) {
            ax.p.set("tick_spec_type", "none");  // как в Grace: подписи без положений не действуют
            return;
        }
        if ((m = re("^(tick|ticklabel) type spec$").match(rest)).hasMatch()) {
            if (m.captured(1) == "ticklabel")
                ax.p.set("tick_spec_type", "both");
            else if (ax.p.s("tick_spec_type") == "none")
                ax.p.set("tick_spec_type", "ticks");
            return;
        }
        if (re("^(ticklabel sign|ticklabel type|tick type|ticklabel layout|tick minor \\S+$)").match(rest).hasMatch())
            return;  // устаревшие команды
        if (!applySchema(axisSchema(), ax.p, rest))
            extra(true);
    }

    void setCmd(DataSet &s, const QString &rest)
    {
        QRegularExpressionMatch m;
        if ((m = re("^errorbar length (\\S+)$").match(rest)).hasMatch()) {
            s.p.set("errorbar_size", m.captured(1).toDouble());
            return;
        }
        const QString key = QString("%1.%2").arg(curGraph).arg(s.id);
        if ((m = re("^fill with (color|pattern|none)$").match(rest)).hasMatch()) {
            fillObs[key] = m.captured(1);
            return;
        }
        if (fileVersion <= 40102 && (m = re("^fill (color|pattern) (\\S+)$").match(rest)).hasMatch()) {
            const QString obs = fillObs.value(key);
            int v = int(m.captured(2).toDouble());
            if (m.captured(1) == "color") {
                if (fileVersion >= 30000)
                    v = obs == "color" ? v : obs == "pattern" ? 1 : 0;
                s.p.set("fill_color", v);
            } else {
                v = obs == "color" ? 1 : obs == "pattern" ? v : 0;
                s.p.set("fill_pattern", v);
            }
            return;
        }
        if (re("^(symbol center|errorbar riser on|errorbar riser off|xyz )").match(rest).hasMatch())
            return;
        if ((m = re("^fill (\\d+)$").match(rest)).hasMatch()) {
            int k = m.captured(1).toInt();
            if (k == 0)
                s.p.set("fill_type", 0);
            else if (k == 1)
                s.p.set("fill_type", 1);
            else if (k == 2 || k == 6 || k == 7) {
                s.p.set("fill_type", 2);
                s.p.set("baseline_type", k == 2 ? 0 : k == 6 ? 3 : 4);
            }
            return;
        }
        if ((m = re("^(skip|linestyle|linewidth|color) (\\S+)$").match(rest)).hasMatch()) {
            const QString w = m.captured(1);
            bool ok = false;
            double v = m.captured(2).toDouble(&ok);
            if (!ok)
                return;
            if (w == "skip")
                s.p.set("symbol_skip", int(v));
            else if (w == "linestyle")
                s.p.set("line_linestyle", int(v));
            else if (w == "linewidth")
                s.p.set("line_linewidth", v);
            else
                s.p.set("line_color", int(v));
            return;
        }
        if ((m = re("^errorbar type (\\w+)$").match(rest)).hasMatch()) {
            s.p.set("errorbar_place", placement(m.captured(1)));
            return;
        }
        if ((m = re("^symbol fill (\\d+)$").match(rest)).hasMatch()) {
            s.p.set("symbol_fill_pattern", m.captured(1).toInt());
            return;
        }
        if (rest.startsWith("type ")) {
            QString t = rest.mid(5).trimmed();
            s.setType(kSetTypes.contains(t) ? t : QString("xy"));
            return;
        }
        if (!applySchema(setSchema(), s.p, rest) && keepExtras)
            s.extra << curLine;
    }

    void objectCmd(const QString &kind, const QString &rest)
    {
        if (curObj < 0 || pr.objects[curObj].kind != kind) {
            if (rest == "def")
                return;
            pr.objects.append(DrawObject(kind));
            curObj = pr.objects.size() - 1;
        }
        DrawObject &o = pr.objects[curObj];
        if (rest == "on" || rest == "off") {
            o.p.set("on", rest == "on");
            return;
        }
        if (rest == "def")
            return;
        QRegularExpressionMatch m = re("^g(\\d+)$").match(rest);
        if (m.hasMatch()) {
            o.graph = m.captured(1).toInt();
            return;
        }
        if ((m = re("^fill with (color|pattern|none)$").match(rest)).hasMatch()) {
            fillObs[QString("obj%1").arg(curObj)] = m.captured(1);
            return;
        }
        applySchema(objectSchema(kind), o.p, rest);
        if (fileVersion <= 40102 && rest.startsWith("fill ") && o.kind != "string" && o.kind != "line") {
            const QString obs = fillObs.value(QString("obj%1").arg(curObj));
            if (obs == "color")
                o.p.set("fill_pattern", 1);
            else if (obs == "pattern")
                o.p.set("fill_color", 1);
            else
                o.p.set("fill_pattern", 0);
        }  // устаревшее молча пропускаем
    }

    void command(const QString &raw)
    {
        const QString cmd = normalizeCommand(raw);
        if (cmd.isEmpty())
            return;
        QRegularExpressionMatch m;
        if ((m = re("^with g(\\d+)$").match(cmd)).hasMatch()) {
            curGraph = m.captured(1).toInt();
            pr.graphOrCreate(curGraph);
            curObj = -1;
            return;
        }
        if ((m = re("^with (string|line|box|ellipse)$").match(cmd)).hasMatch()) {
            pr.objects.append(DrawObject(m.captured(1)));
            curObj = pr.objects.size() - 1;
            return;
        }
        if ((m = re("^(string|line|box|ellipse)(?: (.*))?$").match(cmd)).hasMatch()) {
            objectCmd(m.captured(1), m.captured(2));
            return;
        }
        if ((m = re("^target (?:g(\\d+)\\.)?s(\\d+)$").match(cmd)).hasMatch()) {
            flushBlock();
            flushTarget();
            int gid = m.captured(1).isEmpty() ? (curGraph < 0 ? 0 : curGraph) : m.captured(1).toInt();
            Graph &g = pr.graphOrCreate(gid);
            g.setOrCreate(m.captured(2).toInt());
            tgtGraph = gid;
            tgtSet = m.captured(2).toInt();
            tgtRows.clear();
            return;
        }
        if ((m = re("^type (\\w+)$").match(cmd)).hasMatch()) {
            const QString t = m.captured(1);
            if (tgtGraph >= 0) {
                pr.graph(tgtGraph)->set(tgtSet)->setType(kSetTypes.contains(t) ? t : QString("xy"));
            } else {
                flushBlock();
                pendingType = t;
            }
            return;
        }
        if ((m = re("^g(\\d+)(?: (.*))?$").match(cmd)).hasMatch()) {
            Graph &g = pr.graphOrCreate(m.captured(1).toInt());
            if (!applySchema(graphHeaderSchema(), g.hp, m.captured(2)) && keepExtras)
                g.extra << curLine;
            return;
        }
        if ((m = re("^s(\\d+) (.*)$").match(cmd)).hasMatch()) {
            DataSet &s = graph().setOrCreate(m.captured(1).toInt());
            setCmd(s, m.captured(2));
            return;
        }
        if ((m = re("^(xaxis|yaxis|altxaxis|altyaxis)(?: (.*))?$").match(cmd)).hasMatch()) {
            axisCmd(graph().axis(m.captured(1)), m.captured(2));
            return;
        }
        if (cmd.startsWith("version ")) {
            bool ok = false;
            int v = cmd.mid(8).trimmed().toInt(&ok);
            if (ok) {
                pr.version = v;
                fileVersion = v;
                if (v < 50001 && !fontsMapped) {
                    // старые файлы (ACE/gr, Xmgr): другая нумерация шрифтов
                    static const char *acegr[] = {"Times-Roman", "Times-Bold", "Times-Italic", "Times-BoldItalic",
                                                  "Helvetica", "Helvetica-Bold", "Helvetica-Oblique",
                                                  "Helvetica-BoldOblique", "Symbol", "ZapfDingbats"};
                    pr.fonts.clear();
                    for (int i = 0; i < 10; ++i)
                        pr.fonts.append({i, acegr[i], acegr[i]});
                }
            }
            return;
        }
        if ((m = re("^page size (\\d+)\\s*,?\\s*(\\d+)$").match(cmd)).hasMatch()) {
            pageSizeGiven = true;
            pr.pageW = qMax(1, m.captured(1).toInt());
            pr.pageH = qMax(1, m.captured(2).toInt());
            return;
        }
        if (cmd.startsWith("description ")) {
            pr.descriptions << unquoteString(cmd.mid(12));
            return;
        }
        if ((m = re("^map font (\\d+) to (\".*?\")\\s*,\\s*(\".*\")$").match(cmd)).hasMatch()) {
            int fid = m.captured(1).toInt();
            fontsMapped = true;
            QString name = unquoteString(m.captured(2)), fb = unquoteString(m.captured(3));
            bool found = false;
            for (auto &f : pr.fonts)
                if (f.id == fid) {
                    f.name = name;
                    f.fallback = fb;
                    found = true;
                }
            if (!found)
                pr.fonts.append({fid, name, fb});
            return;
        }
        if ((m = re("^map colou?r (\\d+) to \\((\\d+)\\s*,\\s*(\\d+)\\s*,\\s*(\\d+)\\)\\s*(?:,\\s*(\".*\"))?$").match(cmd)).hasMatch()) {
            int idx = m.captured(1).toInt();
            QColor c(qBound(0, m.captured(2).toInt(), 255), qBound(0, m.captured(3).toInt(), 255),
                     qBound(0, m.captured(4).toInt(), 255));
            pr.colors[idx] = NamedColor{c, m.captured(5).isEmpty() ? QString() : unquoteString(m.captured(5))};
            return;
        }
        if ((m = re("^background colou?r (\\d+)$").match(cmd)).hasMatch()) {
            pr.bgColor = m.captured(1).toInt();
            return;
        }
        if ((m = re("^page background fill (on|off)$").match(cmd)).hasMatch()) {
            pr.bgFill = m.captured(1) == "on";
            return;
        }
        if (re("^(r\\d+ |link |timestamp|default |date |reference |page scroll|page inout|link page)").match(cmd).hasMatch()) {
            extra(false);
            return;
        }
        // команды уровня графика (в xvg — без @with)
        if (graphCmd(graph(), cmd))
            return;
        extra(curGraph >= 0);
    }

    void feed(const QString &text)
    {
        const auto lines = QStringView(text).split('\n');
        for (const auto &l : lines) {
            const QString s = l.trimmed().toString();
            if (s.isEmpty())
                continue;
            curLine = s;
            const QChar c = s[0];
            if (c == '#')
                continue;
            if (c == '@') {
                command(s.mid(1));
                continue;
            }
            if (c == '&') {
                flushTarget();
                flushBlock();
                pendingType.clear();
                continue;
            }
            if (c.isDigit() || c == '+' || c == '-' || c == '.') {
                dataLine(s);
                continue;
            }
            const QString low = s.left(4).toLower();
            if (low.startsWith("nan") || low.startsWith("inf")) {
                dataLine(s);
                continue;
            }
            command(s);  // команды без @ (например, в .par)
        }
    }

    // Пересчёт старых версий формата в современную семантику — как
    // postprocess_project() в Grace.  После этого файл пишется как 5.1.25.
    void postprocessLegacy()
    {
        const int V = fileVersion;
        if (V >= 50125)
            return;
        if (V < 40005)
            pr.pageW = 792, pr.pageH = 612;
        if (V < 50002)
            pr.bgFill = true;
        if (V < 50003) {  // двузначные годы, как в старых версиях
            pr.headerExtra.removeIf([](const QString &l) { return l.contains("date wrap"); });
            pr.headerExtra << "@date wrap on" << "@date wrap year 1900";
        }
        if (V <= 40102) {
            const double ex = pr.pageW / pr.scale(), ey = pr.pageH / pr.scale();
            for (Graph &g : pr.graphs) {
                Vec4 v = g.p.v4("view");
                g.p.setV4("view", v[0] * ex, v[1] * ey, v[2] * ex, v[3] * ey);
                if (g.p.s("legend_loctype") == "view") {
                    Vec2 l = g.p.v2("legend_pos");
                    g.p.setV2("legend_pos", l[0] * ex, l[1] * ey);
                }
                g.p.set("legend_vgap", g.p.i("legend_vgap") - 1);
            }
            for (DrawObject &o : pr.objects) {
                if (o.p.s("loctype") != "view")
                    continue;
                if (o.kind == "string") {
                    Vec2 p = o.p.v2("pos");
                    o.p.setV2("pos", p[0] * ex, p[1] * ey);
                } else {
                    Vec4 c = o.p.v4("coords");
                    o.p.setV4("coords", c[0] * ex, c[1] * ey, c[2] * ex, c[3] * ey);
                }
            }
        }
        for (Graph &g : pr.graphs) {
            for (DataSet &s : g.sets) {
                Props &p = s.p;
                const QString t = s.type();
                if (V < 50000) {
                    const int sym = p.i("symbol");
                    if (sym == 1) {  // «точка» Xmgr
                        p.set("symbol", 1);
                        p.set("symbol_size", 0.0);
                        p.set("symbol_linestyle", 0);
                        p.set("symbol_fill_pattern", 1);
                    } else if (sym > 1) {
                        p.set("symbol", sym - 1);
                    }
                }
                if (V < 40004 && g.type() != "Chart")
                    p.set("symbol_color", p.i("line_color"));
                if (V < 40200)
                    p.set("symbol_fill_color", p.i("symbol_color"));
                if (V <= 40102 && g.type() == "Chart") {
                    s.setType("bar");
                    p.set("symbol_color", p.i("line_color"));
                    p.set("symbol_pattern", p.i("line_pattern"));
                    p.set("symbol_linestyle", p.i("line_linestyle"));
                    p.set("symbol_linewidth", p.d("line_linewidth"));
                    p.set("line_linestyle", 0);
                    p.set("symbol_fill_color", p.i("fill_color"));
                    p.set("symbol_fill_pattern", p.i("fill_pattern"));
                    p.set("fill_pattern", 0);
                }
                if (V <= 40102 && t == "xyhilo")
                    p.set("symbol_linewidth", p.d("line_linewidth"));
                if (V <= 50112 && t == "xyhilo")
                    p.set("avalue", false);
                if (V < 50100 && t == "xyboxplot") {
                    p.set("symbol_linewidth", p.d("line_linewidth"));
                    p.set("symbol_linestyle", p.i("line_linestyle"));
                    p.set("symbol_size", 2.0);
                    p.set("errorbar_riser_linewidth", p.d("line_linewidth"));
                    p.set("errorbar_riser_linestyle", p.i("line_linestyle"));
                    p.set("line_linestyle", 0);
                    p.set("errorbar_size", 0.0);
                }
                if (V < 50003) {
                    p.set("errorbar", true);
                    p.set("errorbar_color", p.i("symbol_color"));
                    p.set("errorbar_pattern", 1);
                    const QString pl = p.s("errorbar_place");
                    if (pl == "normal")
                        p.set("errorbar_place", "opposite");
                    else if (pl == "opposite")
                        p.set("errorbar_place", "normal");
                    else if (t == "xydxdx" || t == "xydydy" || t == "bardydy")
                        p.set("errorbar_place", "normal");
                }
                if (V < 50002)
                    p.set("errorbar_size", p.d("errorbar_size") * 2);
                if (V < 50107 && (t.startsWith("bar") || t == "xyhilo" || t == "xyr" || t == "xyvmap" || t == "xyboxplot"))
                    p.set("symbol_skip", 0);
            }
            if (V < 50105 && g.type() == "Pie") {
                Vec4 w = g.p.v4("world");
                g.p.setV4("world", 0.0, w[1], 2 * M_PI, w[3]);
                g.p.set("xinvert", false);
            }
            for (int a = 0; a < 4; ++a) {
                Props &t = g.axes[a].p;
                const bool isX = a % 2 == 0;
                if (V <= 40102) {
                    if (g.p.s(isX ? "xscale" : "yscale") == "Logarithmic")
                        t.set("tick_major", std::pow(10.0, t.d("tick_major")));
                    t.set("offset", makePair(0, 0));
                }
                if (V < 50000) {
                    t.set("label_side", t.s("ticklabel_place"));
                    Vec2 l = t.v2("label_place_xy");
                    if (!isX)
                        std::swap(l[0], l[1]);
                    t.set("label_place_xy", makePair(l[0], -l[1]));
                }
                if (V >= 50000 && V < 50103 && t.s("label_place_type") == "auto") {
                    t.set("label_place_xy", makePair(0.0, 0.08));
                    t.set("label_place_type", "spec");
                }
            }
        }
        if (V >= 40200 && V <= 50005)  // в этих версиях выравнивание рамки было задано неверно
            for (DrawObject &o : pr.objects)
                if (o.kind == "string")
                    o.p.set("just", o.p.i("just") | JustMiddle);
        pr.version = 50125;
    }

    Project finish()
    {
        flushTarget();
        flushBlock();
        if (pr.fonts.isEmpty())
            for (int i = 0; i < kDefaultFonts.size(); ++i)
                pr.fonts.append({i, kDefaultFonts[i], kDefaultFonts[i]});
        if (pr.graphs.isEmpty())
            pr.graphOrCreate(0);
        postprocessLegacy();
        // подписи без положений (старый формат): ставим на автоматические деления
        for (Graph &g : pr.graphs)
            for (int a = 0; a < 4; ++a) {
                Axis &ax = g.axes[a];
                bool need = !ax.spec.isEmpty() && ax.p.s("tick_spec_type") != "none";
                for (const auto &t : ax.spec)
                    need &= !t.hasValue;
                if (!need)
                    continue;
                const QString st = ax.p.s("tick_spec_type");
                ax.p.set("tick_spec_type", "none");
                QVector<double> majors;
                for (const Tick &t : computeTicks(g, a))
                    if (t.major)
                        majors << t.w;
                ax.p.set("tick_spec_type", st == "none" ? QString("both") : st);
                for (int i = 0; i < ax.spec.size(); ++i) {
                    ax.spec[i].major = true;
                    ax.spec[i].hasValue = true;
                    ax.spec[i].value = majors.value(i, majors.isEmpty() ? double(i) : majors.last() + i);
                }
            }
        return pr;
    }
};

}  // namespace

Project parseAgr(const QString &text, bool keepExtras)
{
    Parser p(keepExtras);
    p.feed(text);
    return p.finish();
}

QString decodeBytes(const QByteArray &raw, QString *encoding)
{
    auto dec = QStringDecoder(QStringDecoder::Utf8);
    QString s = dec.decode(raw);
    if (!dec.hasError()) {
        if (encoding)
            *encoding = "utf-8";
        return s;
    }
    if (encoding)
        *encoding = "latin-1";
    return QString::fromLatin1(raw);
}

bool loadProject(const QString &path, Project &out, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    QString enc;
    const QString text = decodeBytes(f.readAll(), &enc);
    const QString ext = QFileInfo(path).suffix().toLower();
    const bool isData = (ext == "xvg" || ext == "dat" || ext == "txt" || ext == "csv" || ext == "tsv")
                        && !text.trimmed().startsWith("# Grace project");
    out = parseAgr(text, !isData);
    out.encoding = enc;
    if (!isData) {
        out.filename = path;
    } else {
        out.applyPalette(kModernColors);
        applyStyle(out, "modern");
        for (auto &g : out.graphs) {
            styleImported(out, g);
            autoscaleGraph(g);
        }
        fitMargins(out);
        out.filename.clear();
    }
    bool any = false;
    for (const auto &g : out.graphs)
        any |= !g.sets.isEmpty();
    if (isData && !any) {
        if (error)
            *error = T("В файле не найдено числовых данных");
        return false;
    }
    return true;
}

// -----------------------------------------------------------------------------
// Запись

static void writeSchema(QStringList &out, const QString &prefix, const Schema &schema,
                        const Props &p, int skipFirst = 0)
{
    for (int i = skipFirst; i < schema.size(); ++i) {
        const auto &e = schema[i];
        const QString v = formatValue(e.type, p.get(e.name));
        out << (e.kw.isEmpty() ? prefix + v : prefix + e.kw + ' ' + v);
    }
}

static void writeAxis(QStringList &out, const Axis &ax)
{
    const QString pre = QString("@    %1  ").arg(ax.name);
    const Props &p = ax.p;
    out << pre + (p.b("on") ? "on" : "off");
    if (!p.b("on"))
        return;
    const Schema &sc = axisSchema();
    for (int i = 1; i < sc.size(); ++i) {
        const auto &e = sc[i];
        if (e.name == "label_place_xy" && p.s("label_place_type") != "spec")
            continue;
        out << pre + e.kw + ' ' + formatValue(e.type, p.get(e.name));
    }
    const QString st = p.s("tick_spec_type");
    if (st != "none") {
        out << pre + QString("tick spec %1").arg(ax.spec.size());
        for (int j = 0; j < ax.spec.size(); ++j) {
            const auto &t = ax.spec[j];
            if (t.major) {
                out << pre + QString("tick major %1, %2").arg(j).arg(fmtG(t.value, 8));
                if (st == "both")
                    out << pre + QString("ticklabel %1, %2").arg(j).arg(quoteString(t.label));
            } else {
                out << pre + QString("tick minor %1, %2").arg(j).arg(fmtG(t.value, 8));
            }
        }
    }
}

static void writeGraph(QStringList &out, const Graph &g)
{
    const QString gp = QString("@g%1 ").arg(g.id);
    out << gp + (g.hp.b("on") ? "on" : "off");
    writeSchema(out, gp, graphHeaderSchema(), g.hp, 1);
    out << QString("@with g%1").arg(g.id);
    QStringList stack, rest;
    for (const auto &l : g.extra) {
        QString t = l;
        while (t.startsWith('@'))
            t.remove(0, 1);
        (normalizeCommand(t).startsWith("stack world") ? stack : rest) << l;
    }
    for (const auto &e : graphSchema()) {
        out << "@    " + e.kw + ' ' + formatValue(e.type, g.p.get(e.name));
        if (e.name == "world")
            out << stack;
        if (e.name == kGraphAxesAfter)
            for (const auto &ax : g.axes)
                writeAxis(out, ax);
    }
    for (const auto &s : g.sets) {
        writeSchema(out, QString("@    s%1 ").arg(s.id), setSchema(), s.p);
        out << s.extra;
    }
    out << rest;
    if (!g.hp.b("on"))
        out << gp + "off";  // «@with g» включает график — выключаем снова
}

static void writeObject(QStringList &out, const DrawObject &o)
{
    const QString k = o.kind;
    out << "@with " + k;
    out << QString("@    %1 %2").arg(k, o.p.b("on") ? "on" : "off");
    out << QString("@    %1 loctype %2").arg(k, o.p.s("loctype"));
    if (o.p.s("loctype") == "world")
        out << QString("@    %1 g%2").arg(k).arg(o.graph);
    const Schema &sc = objectSchema(k);
    for (int i = 2; i < sc.size(); ++i) {
        const auto &e = sc[i];
        const QString v = formatValue(e.type, o.p.get(e.name));
        out << (e.kw.isEmpty() ? QString("@    %1 %2").arg(k, v) : QString("@    %1 %2 %3").arg(k, e.kw, v));
    }
    if (k != "string")
        out << QString("@%1 def").arg(k);
}

QString writeAgr(const Project &pr)
{
    QStringList out;
    out << "# Grace project file" << "#" << "# Written by NeoGrace" << "#";
    out << QString("@version %1").arg(pr.version);
    out << QString("@page size %1, %2").arg(pr.pageW).arg(pr.pageH);
    for (const auto &d : pr.descriptions)
        out << "@description " + quoteString(d);
    for (const auto &f : pr.fonts)
        out << QString("@map font %1 to \"%2\", \"%3\"").arg(f.id).arg(f.name, f.fallback);
    for (auto it = pr.colors.begin(); it != pr.colors.end(); ++it)
        out << QString("@map color %1 to (%2, %3, %4), \"%5\"")
                   .arg(it.key())
                   .arg(it->rgb.red())
                   .arg(it->rgb.green())
                   .arg(it->rgb.blue())
                   .arg(it->name);
    out << pr.headerExtra;
    out << QString("@background color %1").arg(pr.bgColor);
    out << QString("@page background fill %1").arg(pr.bgFill ? "on" : "off");
    for (const auto &o : pr.objects)
        writeObject(out, o);
    for (const auto &g : pr.graphs)
        writeGraph(out, g);
    QString text = out.join('\n') + '\n';
    for (const auto &g : pr.graphs) {
        for (const auto &s : g.sets) {
            const int n = s.size();
            if (n == 0)
                continue;
            text += QString("@target G%1.S%2\n@type %3\n").arg(g.id).arg(s.id).arg(s.type());
            bool withStr = false;
            for (const QString &st : s.strings)
                withStr |= !st.isEmpty();
            QString row;
            for (int i = 0; i < n; ++i) {
                row.clear();
                for (int k = 0; k < s.cols.size(); ++k) {
                    if (k)
                        row += ' ';
                    row += fmtG(s.cols[k].value(i, NaN));
                }
                if (withStr)
                    row += ' ' + quoteString(s.strings.value(i));
                row += '\n';
                text += row;
            }
            text += "&\n";
        }
    }
    return text;
}

bool saveProject(Project &pr, const QString &path, QString *error)
{
    const QString text = writeAgr(pr);
    QByteArray data;
    if (pr.encoding == "utf-8") {
        data = text.toUtf8();
    } else {
        bool latinOk = true;
        for (QChar c : text)
            if (c.unicode() > 255) {
                latinOk = false;
                break;
            }
        if (latinOk) {
            data = text.toLatin1();
        } else {
            data = text.toUtf8();
            pr.encoding = "utf-8";
        }
    }
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    f.write(data);
    if (!f.commit()) {
        if (error)
            *error = f.errorString();
        return false;
    }
    pr.filename = path;
    return true;
}

// -----------------------------------------------------------------------------
// Таблицы

bool parseNumberLine(QStringView line, QVector<double> &out)
{
    out.clear();
    return parseNumbers(line, out) && !out.isEmpty();
}

QVector<Table> readTables(const QString &text, bool blankSplits)
{
    QVector<Table> out;
    QVector<QVector<double>> rows;
    QStringList header, commentHeader;
    auto flush = [&]() {
        if (rows.isEmpty())
            return;
        int n = 0;
        for (const auto &r : rows)
            n = qMax(n, int(r.size()));
        Table t;
        t.cols = QVector<QVector<double>>(n);
        for (auto &c : t.cols)
            c.reserve(rows.size());
        for (const auto &r : rows)
            for (int k = 0; k < n; ++k)
                t.cols[k].append(k < r.size() ? r[k] : NaN);
        if (header.size() == n)
            t.headers = header;
        else if (commentHeader.size() == n)
            t.headers = commentHeader;
        out << t;
        rows.clear();
    };
    static const QRegularExpression sep("[\\s,;]+");
    for (const auto &l : QStringView(text).split('\n')) {
        const QString s = l.trimmed().toString();
        if (s.isEmpty()) {
            if (blankSplits)
                flush();
            continue;
        }
        if (s.startsWith('&')) {
            flush();
            continue;
        }
        const QChar c = s[0];
        if (c == '#' || c == '@' || c == '%' || c == '!' || c == ';') {
            if (c == '#' && rows.isEmpty())
                commentHeader = s.mid(1).split(sep, Qt::SkipEmptyParts);
            continue;
        }
        QVector<double> r;
        bool ok = parseNumbers(QStringView(s), r);
        if (!ok) {
            if (rows.isEmpty()) {  // строка заголовка
                header.clear();
                for (const auto &p : s.split(sep, Qt::SkipEmptyParts))
                    header << QString(p).remove('"');
            }
            continue;
        }
        if (!r.isEmpty())
            rows << r;
    }
    flush();
    return out;
}

}  // namespace ng
