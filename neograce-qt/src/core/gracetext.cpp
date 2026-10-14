#include "gracetext.h"

#include "model.h"

#include <QFontDatabase>
#include <QFontMetricsF>
#include <QPaintDevice>
#include <QPainter>
#include <QTransform>
#include <cmath>

namespace ng {

// Шрифт Symbol (Adobe) → Unicode
static const QHash<QChar, QChar> &symbolMap()
{
    static QHash<QChar, QChar> m;
    if (m.isEmpty()) {
        const char16_t *lat = u"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
        const char16_t *grk = u"αβχδεφγηιϕκλμνοπθρστυϖωξψζΑΒΧΔΕΦΓΗΙϑΚΛΜΝΟΠΘΡΣΤΥςΩΞΨΖ";
        for (int i = 0; lat[i]; ++i)
            m.insert(QChar(lat[i]), QChar(grk[i]));
        const struct { char16_t a, b; } extra[] = {
            {u'"', u'∀'}, {u'$', u'∃'}, {u'\'', u'∋'}, {u'*', u'∗'}, {u'@', u'≅'},
            {u'\\', u'∴'}, {u'^', u'⊥'}, {u'`', u'‾'}, {u'~', u'∼'},
            {0xa1, u'ϒ'}, {0xa2, u'′'}, {0xa3, u'≤'}, {0xa4, u'⁄'}, {0xa5, u'∞'},
            {0xa6, u'ƒ'}, {0xa7, u'♣'}, {0xa8, u'♦'}, {0xa9, u'♥'}, {0xaa, u'♠'},
            {0xab, u'↔'}, {0xac, u'←'}, {0xad, u'↑'}, {0xae, u'→'}, {0xaf, u'↓'},
            {0xb0, u'°'}, {0xb1, u'±'}, {0xb2, u'″'}, {0xb3, u'≥'}, {0xb4, u'×'},
            {0xb5, u'∝'}, {0xb6, u'∂'}, {0xb7, u'•'}, {0xb8, u'÷'}, {0xb9, u'≠'},
            {0xba, u'≡'}, {0xbb, u'≈'}, {0xbc, u'…'}, {0xbd, u'|'}, {0xbe, u'—'},
            {0xbf, u'↵'}, {0xc0, u'ℵ'}, {0xc1, u'ℑ'}, {0xc2, u'ℜ'}, {0xc3, u'℘'},
            {0xc4, u'⊗'}, {0xc5, u'⊕'}, {0xc6, u'∅'}, {0xc7, u'∩'}, {0xc8, u'∪'},
            {0xc9, u'⊃'}, {0xca, u'⊇'}, {0xcb, u'⊄'}, {0xcc, u'⊂'}, {0xcd, u'⊆'},
            {0xce, u'∈'}, {0xcf, u'∉'}, {0xd0, u'∠'}, {0xd1, u'∇'}, {0xd2, u'®'},
            {0xd3, u'©'}, {0xd4, u'™'}, {0xd5, u'∏'}, {0xd6, u'√'}, {0xd7, u'⋅'},
            {0xd8, u'¬'}, {0xd9, u'∧'}, {0xda, u'∨'}, {0xdb, u'⇔'}, {0xdc, u'⇐'},
            {0xdd, u'⇑'}, {0xde, u'⇒'}, {0xdf, u'⇓'}, {0xe0, u'◊'}, {0xe1, u'⟨'},
            {0xe5, u'∑'}, {0xf1, u'⟩'}, {0xf2, u'∫'},
        };
        for (const auto &e : extra)
            m.insert(QChar(e.a), QChar(e.b));
    }
    return m;
}

QString symbolToUnicode(const QString &s)
{
    const auto &m = symbolMap();
    QString out = s;
    for (auto &c : out)
        c = m.value(c, c);
    return out;
}

QString unicodeToGrace(const QString &s)
{
    static QHash<QChar, QChar> rev;
    if (rev.isEmpty()) {
        const auto &m = symbolMap();
        for (auto it = m.begin(); it != m.end(); ++it)
            if (it.key().isLetter() && it.key().unicode() < 128)
                rev.insert(it.value(), it.key());
    }
    QString out;
    for (QChar c : s) {
        auto it = rev.find(c);
        if (it != rev.end())
            out += QStringLiteral("\\x") + *it + QStringLiteral("\\f{}");
        else
            out += c;
    }
    return out;
}

QVector<TextLine> parseGraceText(const QString &s, int baseFont,
                                 const std::function<int(const QString &)> &lookup)
{
    QVector<TextLine> lines(1);
    TextRun cur;
    cur.font = baseFont;
    QString buf;
    auto flush = [&]() {
        if (!buf.isEmpty()) {
            TextRun r = cur;
            r.text = buf;
            lines.last().append(r);
            buf.clear();
        }
    };
    const int n = s.size();
    int i = 0;
    while (i < n) {
        const QChar c = s[i];
        if (c == '\n') {
            flush();
            lines.append(TextLine());
            cur.vshift = 0;
            cur.hshift = 0;
            ++i;
            continue;
        }
        if (c != '\\' || i + 1 >= n) {
            buf += c;
            ++i;
            continue;
        }
        const QChar e = s[i + 1];
        i += 2;
        QString arg;
        bool hasArg = false;
        static const QString withArgs = QStringLiteral("fRzZvVhHrlktTmMb#");
        if (i < n && s[i] == '{' && withArgs.contains(e)) {
            int j = s.indexOf('}', i);
            if (j < 0)
                j = n;
            arg = s.mid(i + 1, j - i - 1);
            hasArg = true;
            i = j + 1;
        }
        const char ec = e.toLatin1();
        switch (ec) {
        case '\\': buf += '\\'; break;
        case '"': buf += '"'; break;
        case 'n':
            flush();
            lines.append(TextLine());
            cur.vshift = 0;
            cur.hshift = 0;
            break;
        case 's':
            flush();
            cur.vshift -= cur.scale * 0.4;
            cur.scale *= M_SQRT1_2;
            break;
        case 'S':
            flush();
            cur.vshift += cur.scale * 0.6;
            cur.scale *= M_SQRT1_2;
            break;
        case 'N':
            flush();
            cur.vshift = 0;
            cur.scale = 1.0;
            break;
        case '+':
            flush();
            cur.scale *= std::sqrt(M_SQRT2);
            break;
        case '-':
            flush();
            cur.scale /= std::sqrt(M_SQRT2);
            break;
        case 'z':
            flush();
            if (hasArg && !arg.isEmpty())
                cur.scale *= qBound(0.05, arg.toDouble(), 20.0);
            else
                cur.scale = 1.0;
            break;
        case 'Z':
            flush();
            cur.scale = hasArg ? qBound(0.05, arg.toDouble(), 20.0) : 1.0;
            break;
        case 'v':
            flush();
            cur.vshift += arg.toDouble() * cur.scale;
            break;
        case 'V':
            flush();
            cur.vshift = arg.toDouble();
            break;
        case 'h':
            flush();
            cur.hshift += arg.toDouble() * cur.scale;
            break;
        case 'x':
            flush();
            cur.font = lookup(QStringLiteral("Symbol"));
            break;
        case 'f':
            flush();
            cur.font = (!hasArg || arg.isEmpty()) ? baseFont : lookup(arg);
            break;
        case 'u': flush(); cur.under = true; break;
        case 'U': flush(); cur.under = false; break;
        case 'o': flush(); cur.over = true; break;
        case 'O': flush(); cur.over = false; break;
        case 'R':
            flush();
            if (!hasArg || arg.isEmpty()) {
                cur.color = -1;
            } else {
                bool ok = false;
                int k = arg.toInt(&ok);
                cur.color = ok ? k : lookup(QStringLiteral("#color:") + arg);
            }
            break;
        case '#': {
            bool ok = false;
            int code = arg.toInt(&ok, 16);
            if (ok && code > 0)
                buf += QChar(code);
            break;
        }
        default:
            if (e.isDigit()) {
                flush();
                cur.font = e.digitValue();
            } else if (QStringLiteral("cCqQlLkKtTmMbBdDiIrRAaEeHjJOpPwWyY").contains(e)) {
                // прочие сдвиги/наклоны/трансформации — игнорируем
            } else {
                buf += e;
            }
        }
    }
    flush();
    return lines;
}

QString stripEscapes(const QString &s)
{
    if (!s.contains('\\'))
        return s;
    auto lk = [](const QString &name) -> int {
        const QString l = name.toLower();
        if (l.startsWith("#color:"))
            return -1;
        if (l == "symbol" || l == "12")
            return 12;
        bool ok = false;
        int k = name.toInt(&ok);
        return ok ? k : 0;
    };
    QStringList out;
    for (const auto &line : parseGraceText(s, 0, lk)) {
        QString t;
        for (const auto &r : line)
            t += r.font == 12 ? symbolToUnicode(r.text) : r.text;
        out << t;
    }
    return out.join(' ');
}

// ---------------------------------------------------------------------------

FontStyle graceFontStyle(const QString &name)
{
    FontStyle st;
    const QString n = name.toLower();
    static const QStringList serif = {"Times New Roman", "Times", "Nimbus Roman", "TeX Gyre Termes",
                                      "Liberation Serif", "FreeSerif", "DejaVu Serif"};
    static const QStringList sans = {"Helvetica", "Helvetica Neue", "Arial", "Nimbus Sans",
                                     "TeX Gyre Heros", "Liberation Sans", "FreeSans", "DejaVu Sans"};
    static const QStringList mono = {"Courier New", "Courier", "Nimbus Mono PS", "Liberation Mono",
                                     "FreeMono", "DejaVu Sans Mono"};
    if (n.contains("symbol")) {
        st.symbol = true;
        st.families = serif;
        return st;
    }
    if (n.contains("helvetica") || n.contains("sans") || n.contains("arial"))
        st.families = sans;
    else if (n.contains("courier") || n.contains("mono"))
        st.families = mono;
    else if (n.contains("times") || n.contains("roman") || n.contains("serif") || n.contains("dingbat"))
        st.families = serif;
    else {
        // произвольное имя: пробуем как есть (без суффиксов -Bold и т.п.)
        QString fam = name;
        int dash = fam.indexOf('-');
        if (dash > 0)
            fam = fam.left(dash);
        st.families = QStringList{fam} + serif;
    }
    st.italic = n.contains("italic") || n.contains("oblique");
    st.bold = n.contains("bold");
    return st;
}

TextEngine::TextEngine(const Project &pr, QPaintDevice *dev) : m_pr(pr), m_dev(dev)
{
    m_dpi = dev ? dev->logicalDpiY() : 72.0;
    if (m_dpi <= 0)
        m_dpi = 72.0;
}

bool TextEngine::isSymbolFont(int fid) const
{
    return m_pr.fontName(fid).toLower().contains("symbol");
}

int TextEngine::lookupFont(const QString &name) const
{
    const QString s = name.trimmed();
    if (s.startsWith("#color:")) {
        const QString cn = s.mid(7).trimmed().toLower();
        for (auto it = m_pr.colors.begin(); it != m_pr.colors.end(); ++it)
            if (it->name.toLower() == cn)
                return it.key();
        return -1;
    }
    bool ok = false;
    int k = s.toInt(&ok);
    if (ok)
        return k;
    const QString sl = s.toLower();
    for (const auto &f : m_pr.fonts)
        if (f.name.toLower() == sl)
            return f.id;
    if (sl == "symbol")
        return 12;
    for (const auto &f : m_pr.fonts)
        if (f.name.toLower().contains(sl))
            return f.id;
    return 0;
}

QFont TextEngine::font(int fid, double px) const
{
    const QString key = QString::number(fid) + '|' + QString::number(px, 'f', 3);
    auto it = m_fontCache.find(key);
    if (it != m_fontCache.end())
        return *it;
    FontStyle st = graceFontStyle(m_pr.fontName(fid));
    QFont f;
    f.setFamilies(st.families);
    f.setFamily(st.families.first());
    f.setItalic(st.italic);
    f.setBold(st.bold);
    f.setPointSizeF(qMax(0.1, px * 72.0 / m_dpi));
    f.setHintingPreference(QFont::PreferNoHinting);
    f.setStyleStrategy(QFont::PreferAntialias);
    m_fontCache.insert(key, f);
    return f;
}

TextEngine::Layout TextEngine::layout(const QString &s, int fid, double px) const
{
    const QString key = QString::number(fid) + '|' + QString::number(px, 'f', 3) + '|' + s;
    auto cached = m_layoutCache.find(key);
    if (cached != m_layoutCache.end())
        return *cached;

    Layout L;
    auto lk = [this](const QString &n) { return lookupFont(n); };
    const auto lines = parseGraceText(s, fid, lk);
    const double lineSkip = 1.2 * px;
    double top = 1e300, bottom = -1e300, maxw = 0;
    bool any = false;
    for (int li = 0; li < lines.size(); ++li) {
        double x = 0;
        const double by = li * lineSkip;
        for (const TextRun &r : lines[li]) {
            const double size = px * r.scale;
            QString text = r.text;
            QFont f;
            if (isSymbolFont(r.font)) {
                text = symbolToUnicode(text);
                // греческие буквы — прямым начертанием основного шрифта строки
                const bool baseOk = !isSymbolFont(fid)
                    && graceFontStyle(m_pr.fontName(fid)).families.first() != "Courier New";
                f = font(baseOk ? fid : 0, size);
                f.setItalic(false);
            } else {
                f = font(r.font, size);
            }
            QFontMetricsF fm(f, m_dev);
            Piece pc;
            pc.text = text;
            pc.font = f;
            pc.pos = QPointF(x + r.hshift * px, by - r.vshift * px);
            pc.width = fm.horizontalAdvance(text);
            pc.under = r.under;
            pc.over = r.over;
            pc.color = r.color;
            pc.px = size;
            QRectF ink = fm.tightBoundingRect(text);
            if (!ink.isEmpty() || ink.height() > 0) {
                top = qMin(top, pc.pos.y() + ink.top());
                bottom = qMax(bottom, pc.pos.y() + ink.bottom());
                any = true;
            } else if (!text.trimmed().isEmpty()) {
                top = qMin(top, pc.pos.y() - fm.ascent());
                bottom = qMax(bottom, pc.pos.y() + fm.descent());
                any = true;
            }
            x = pc.pos.x() + pc.width;
            maxw = qMax(maxw, x);
            L.pieces.append(pc);
        }
    }
    if (!any) {
        top = 0;
        bottom = 0;
    }
    L.box = QRectF(QPointF(0, top), QPointF(maxw, bottom));
    if (m_layoutCache.size() > 4000)
        m_layoutCache.clear();
    m_layoutCache.insert(key, L);
    return L;
}

// Как в Grace: выравнивание по «бумажной» (невращённой) рамке повёрнутого
// текста; для базовой линии — вдоль направления строки.
QTransform TextEngine::placement(const QRectF &box, QPointF anchor, double angleDeg, int just) const
{
    QTransform rot;
    rot.rotate(-angleDeg);
    double hf = 0;
    switch (just & 3) {
    case JustRight: hf = 1; break;
    case JustCenter: hf = 0.5; break;
    default: hf = 0;
    }
    QPointF off;
    if ((just & 12) == JustBline) {
        off = rot.map(QPointF(box.left() + hf * box.width(), 0));
    } else {
        double vf = 0;
        switch (just & 12) {
        case JustTop: vf = 1; break;
        case JustMiddle: vf = 0.5; break;
        default: vf = 0;
        }
        QRectF rb = rot.mapRect(box);
        off = QPointF(rb.left() + hf * rb.width(), rb.bottom() - vf * rb.height());
    }
    QTransform t;
    t.translate(anchor.x() - off.x(), anchor.y() - off.y());
    t.rotate(-angleDeg);
    return t;
}

QRectF TextEngine::bounds(const QString &s, int fid, double px, int just) const
{
    if (s.isEmpty())
        return QRectF();
    Layout L = layout(s, fid, px);
    return placement(L.box, QPointF(0, 0), 0, just).mapRect(L.box);
}

QPolygonF TextEngine::outline(const QString &s, int fid, double px, QPointF anchor,
                              double angleDeg, int just) const
{
    if (s.isEmpty())
        return QPolygonF();
    Layout L = layout(s, fid, px);
    return placement(L.box, anchor, angleDeg, just).map(QPolygonF(L.box));
}

QPolygonF TextEngine::draw(QPainter *p, const QString &s, int fid, double px,
                           const QColor &color, QPointF anchor, double angleDeg, int just) const
{
    if (s.isEmpty() || px <= 0)
        return QPolygonF();
    Layout L = layout(s, fid, px);
    const QTransform t = placement(L.box, anchor, angleDeg, just);
    p->save();
    p->setTransform(t, true);
    for (const Piece &pc : L.pieces) {
        QColor c = color;
        if (pc.color >= 0)
            c = m_pr.color(pc.color);
        p->setFont(pc.font);
        p->setPen(c);
        p->drawText(pc.pos, pc.text);
        if (pc.under || pc.over) {
            QFontMetricsF fm(pc.font, m_dev);
            QPen lp(c, qMax(0.5, fm.lineWidth()));
            p->setPen(lp);
            if (pc.under) {
                double y = pc.pos.y() + fm.underlinePos();
                p->drawLine(QPointF(pc.pos.x(), y), QPointF(pc.pos.x() + pc.width, y));
            }
            if (pc.over) {
                double y = pc.pos.y() - fm.ascent() * 0.92;
                p->drawLine(QPointF(pc.pos.x(), y), QPointF(pc.pos.x() + pc.width, y));
            }
        }
    }
    p->restore();
    return t.map(QPolygonF(L.box));
}

}  // namespace ng
