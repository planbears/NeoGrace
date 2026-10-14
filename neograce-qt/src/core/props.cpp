#include "props.h"

#include <QHash>
#include <algorithm>
#include <cmath>

namespace ng {

QVariant makePair(double a, double b)
{
    return QVariant::fromValue(QList<double>{a, b});
}

QVariant makeQuad(double a, double b, double c, double d)
{
    return QVariant::fromValue(QList<double>{a, b, c, d});
}

Props::Props(const Schema &s)
{
    for (const auto &e : s) {
        if (!m_v.contains(e.name))
            m_v.insert(e.name, e.def);
    }
}

Vec2 Props::v2(const QString &k) const
{
    auto l = m_v.value(k).value<QList<double>>();
    Vec2 r{0, 0};
    for (int i = 0; i < 2 && i < l.size(); ++i)
        r[i] = l[i];
    return r;
}

Vec4 Props::v4(const QString &k) const
{
    auto l = m_v.value(k).value<QList<double>>();
    Vec4 r{0, 0, 0, 0};
    for (int i = 0; i < 4 && i < l.size(); ++i)
        r[i] = l[i];
    return r;
}

const PropDef *findDef(const Schema &s, const QString &name)
{
    for (const auto &e : s)
        if (e.name == name)
            return &e;
    return nullptr;
}

QString fmtG(double v, int prec)
{
    if (std::isnan(v))
        return QStringLiteral("nan");
    if (std::isinf(v))
        return v > 0 ? QStringLiteral("inf") : QStringLiteral("-inf");
    return QString::number(v, 'g', prec);
}

QString unquoteString(const QString &v0, bool *ok)
{
    QString v = v0.trimmed();
    if (!v.startsWith('"')) {
        if (ok)
            *ok = false;
        return QString();
    }
    if (ok)
        *ok = true;
    // ищем закрывающую кавычку, не экранированную обратной косой чертой
    int end = -1;
    for (int i = 1; i < v.size(); ++i) {
        if (v[i] == '\\' && i + 1 < v.size()) {
            ++i;
            continue;
        }
        if (v[i] == '"')
            end = i;
    }
    QString body = end > 0 ? v.mid(1, end - 1) : v.mid(1);
    // Grace хранит \" внутри строки; снимаем только это экранирование
    body.replace(QStringLiteral("\\\""), QStringLiteral("\""));
    return body;
}

QString quoteString(const QString &s)
{
    QString out;
    out.reserve(s.size() + 2);
    out += '"';
    for (int i = 0; i < s.size(); ++i) {
        QChar c = s[i];
        if (c == '\\' && i + 1 < s.size()) {
            out += c;
            out += s[i + 1];
            ++i;
            continue;
        }
        if (c == '"')
            out += QStringLiteral("\\\"");
        else if (c == '\n')
            out += QStringLiteral("\\n");
        else
            out += c;
    }
    out += '"';
    return out;
}

static bool parseNumbers(const QString &v, int n, QList<double> &out)
{
    const auto parts = v.split(',');
    if (parts.size() != n)
        return false;
    out.clear();
    for (const auto &p : parts) {
        bool ok = false;
        double d = p.trimmed().toDouble(&ok);
        if (!ok)
            return false;
        out << d;
    }
    return true;
}

bool parseValue(PT t, const QString &v0, const QStringList &choices, QVariant &out)
{
    const QString v = v0.trimmed();
    switch (t) {
    case PT::OnOff:
    case PT::TF:
    case PT::Flag:
        if (v == "on" || v == "true") {
            out = true;
            return true;
        }
        if (v == "off" || v == "false") {
            out = false;
            return true;
        }
        return false;
    case PT::Int: {
        bool ok = false;
        double d = v.toDouble(&ok);
        if (!ok)
            return false;
        out = int(std::lround(d));
        return true;
    }
    case PT::Float:
    case PT::GFloat:
    case PT::LW: {
        bool ok = false;
        double d = v.toDouble(&ok);
        if (!ok)
            return false;
        out = d;
        return true;
    }
    case PT::Str: {
        bool ok = false;
        QString s = unquoteString(v, &ok);
        if (!ok)
            return false;
        out = s;
        return true;
    }
    case PT::Word:
        if (v.isEmpty() || v.contains(' '))
            return false;
        if (!choices.isEmpty()) {
            for (const auto &c : choices)
                if (c.compare(v, Qt::CaseInsensitive) == 0) {
                    out = c;
                    return true;
                }
            return false;
        }
        out = v;
        return true;
    case PT::Pair:
    case PT::PairG:
    case PT::PairSp: {
        QList<double> l;
        if (!parseNumbers(v, 2, l))
            return false;
        out = QVariant::fromValue(l);
        return true;
    }
    case PT::IntPair: {
        QList<double> l;
        if (!parseNumbers(v, 2, l))
            return false;
        l[0] = std::round(l[0]);
        l[1] = std::round(l[1]);
        out = QVariant::fromValue(l);
        return true;
    }
    case PT::Quad:
    case PT::QuadG: {
        QList<double> l;
        if (!parseNumbers(v, 4, l))
            return false;
        out = QVariant::fromValue(l);
        return true;
    }
    case PT::Rest:
        out = v;
        return true;
    }
    return false;
}

static QString f6(double d) { return QString::asprintf("%f", d); }

QString formatValue(PT t, const QVariant &v)
{
    switch (t) {
    case PT::OnOff:
    case PT::Flag:
        return v.toBool() ? "on" : "off";
    case PT::TF:
        return v.toBool() ? "true" : "false";
    case PT::Int:
        return QString::number(v.toInt());
    case PT::Float:
        return f6(v.toDouble());
    case PT::GFloat:
        return fmtG(v.toDouble());
    case PT::LW:
        return QString::asprintf("%.1f", v.toDouble());
    case PT::Str:
        return quoteString(v.toString());
    case PT::Word:
    case PT::Rest:
        return v.toString();
    case PT::Pair: {
        auto l = v.value<QList<double>>();
        while (l.size() < 2) l << 0.0;
        return f6(l[0]) + ", " + f6(l[1]);
    }
    case PT::PairG: {
        auto l = v.value<QList<double>>();
        while (l.size() < 2) l << 0.0;
        return fmtG(l[0]) + ", " + fmtG(l[1]);
    }
    case PT::PairSp: {
        auto l = v.value<QList<double>>();
        while (l.size() < 2) l << 0.0;
        return f6(l[0]) + " , " + f6(l[1]);
    }
    case PT::IntPair: {
        auto l = v.value<QList<double>>();
        while (l.size() < 2) l << 0.0;
        return QString::number(qRound(l[0])) + ", " + QString::number(qRound(l[1]));
    }
    case PT::Quad: {
        auto l = v.value<QList<double>>();
        while (l.size() < 4) l << 0.0;
        return f6(l[0]) + ", " + f6(l[1]) + ", " + f6(l[2]) + ", " + f6(l[3]);
    }
    case PT::QuadG: {
        auto l = v.value<QList<double>>();
        while (l.size() < 4) l << 0.0;
        return fmtG(l[0]) + ", " + fmtG(l[1]) + ", " + fmtG(l[2]) + ", " + fmtG(l[3]);
    }
    }
    return QString();
}

// Схема, отсортированная по убыванию длины ключевого слова: сначала пробуем
// самые специфичные ("tick major size" раньше "tick major").
static const QVector<const PropDef *> &sortedSchema(const Schema &schema)
{
    static QHash<const Schema *, QVector<const PropDef *>> cache;
    auto it = cache.find(&schema);
    if (it != cache.end() && it->size() == schema.size())
        return *it;
    QVector<const PropDef *> v;
    for (const auto &e : schema)
        v << &e;
    std::stable_sort(v.begin(), v.end(), [](const PropDef *a, const PropDef *b) {
        return a->kw.trimmed().size() > b->kw.trimmed().size();
    });
    cache[&schema] = v;
    return cache[&schema];
}

bool applySchema(const Schema &schema, Props &p, const QString &rest)
{
    for (const PropDef *e : sortedSchema(schema)) {
        const QString kw = e->kw.trimmed();
        QString val;
        if (kw.isEmpty())
            val = rest;
        else if (rest.startsWith(kw + ' '))
            val = rest.mid(kw.size() + 1);
        else
            continue;
        QVariant out;
        if (parseValue(e->type, val, e->choices, out)) {
            p.set(e->name, out);
            return true;
        }
    }
    return false;
}

}  // namespace ng
