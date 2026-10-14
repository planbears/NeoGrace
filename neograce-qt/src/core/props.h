// Схемо-ориентированные свойства объектов Grace.
//
// Каждое свойство описано один раз: внутреннее имя, ключевое слово в .agr,
// тип значения, значение по умолчанию и (для перечислений) допустимые слова.
// Одна и та же схема используется парсером, писателем и инспектором,
// поэтому формат файла и интерфейс не расходятся.
#pragma once

#include <QHash>
#include <QList>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>
#include <array>

namespace ng {

enum class PT {
    OnOff,   // on/off
    TF,      // true/false
    Flag,    // on/off (для объектов)
    Int,
    Float,   // %f
    GFloat,  // %.12g
    LW,      // толщина линии, %.1f
    Str,     // "строка"
    Word,    // слово из списка
    Pair,    // x, y  (%f)
    PairG,   // x, y  (%.12g)
    PairSp,  // x , y (Grace пишет пробел перед запятой)
    IntPair,
    Quad,    // 4 числа (%f)
    QuadG,   // 4 числа (%.12g)
    Rest     // остаток строки как есть
};

struct PropDef {
    QString name;
    QString kw;
    PT type;
    QVariant def;
    QStringList choices;
};

using Schema = QVector<PropDef>;

using Vec2 = std::array<double, 2>;
using Vec4 = std::array<double, 4>;

QVariant makePair(double a, double b);
QVariant makeQuad(double a, double b, double c, double d);

class Props {
public:
    Props() = default;
    explicit Props(const Schema &s);

    bool has(const QString &k) const { return m_v.contains(k); }
    QVariant get(const QString &k) const { return m_v.value(k); }
    void set(const QString &k, const QVariant &v) { m_v[k] = v; }

    bool b(const QString &k) const { return m_v.value(k).toBool(); }
    int i(const QString &k) const { return m_v.value(k).toInt(); }
    double d(const QString &k) const { return m_v.value(k).toDouble(); }
    QString s(const QString &k) const { return m_v.value(k).toString(); }
    Vec2 v2(const QString &k) const;
    Vec4 v4(const QString &k) const;
    void setV2(const QString &k, double a, double b) { m_v[k] = makePair(a, b); }
    void setV4(const QString &k, double a, double b, double c, double d)
    {
        m_v[k] = makeQuad(a, b, c, d);
    }

    const QHash<QString, QVariant> &all() const { return m_v; }
    bool operator==(const Props &o) const { return m_v == o.m_v; }

private:
    QHash<QString, QVariant> m_v;
};

const PropDef *findDef(const Schema &s, const QString &name);

// Разбор и форматирование значений .agr
bool parseValue(PT t, const QString &v, const QStringList &choices, QVariant &out);
QString formatValue(PT t, const QVariant &v);
QString quoteString(const QString &s);
QString unquoteString(const QString &v, bool *ok = nullptr);
QString fmtG(double v, int prec = 12);

// Применить строку команды к свойствам (true — поняли строку).
bool applySchema(const Schema &schema, Props &p, const QString &rest);

}  // namespace ng
