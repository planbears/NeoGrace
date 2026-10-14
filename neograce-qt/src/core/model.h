// Модель проекта Grace (.agr).
#pragma once

#include "props.h"

#include <QColor>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

namespace ng {

// ---- константы Grace -------------------------------------------------------

extern const QStringList kDefaultFonts;
struct NamedColor {
    QColor rgb;
    QString name;
};
extern const QVector<NamedColor> kClassicColors;
extern const QVector<NamedColor> kModernColors;
// Палитры: цвета 0…15 с теми же именами (red остаётся «красным» в своём оттенке)
struct PaletteDef {
    QString key;
    const char *name;  // по-русски; в интерфейсе — через T()
    QVector<NamedColor> colors;
};
const QVector<PaletteDef> &paletteDefs();
const QVector<NamedColor> *paletteColors(const QString &key);

extern const QStringList kSetTypes;
int setTypeColumns(const QString &type);
QStringList setColumnNames(const QString &type);

extern const QStringList kAxisNames;  // xaxis yaxis altxaxis altyaxis
extern const QStringList kPlacement;  // normal opposite both
extern const QStringList kTickLabelFormats;
extern const QStringList kScaleTypes;  // Normal Logarithmic Reciprocal Logit
extern const QStringList kGraphTypes;

const Schema &graphHeaderSchema();
const Schema &graphSchema();
const Schema &axisSchema();
const Schema &setSchema();
const Schema &objectSchema(const QString &kind);
extern const QString kGraphAxesAfter;  // после какого свойства графика пишутся оси

// ---- объекты ---------------------------------------------------------------

struct TickSpec {
    bool major = true;
    double value = 0;
    QString label;
    bool hasValue = true;  // false — задана только подпись (старый формат)
    bool operator==(const TickSpec &o) const
    {
        return major == o.major && value == o.value && label == o.label;
    }
};

struct Axis {
    QString name = "xaxis";
    Props p;
    QVector<TickSpec> spec;
    Axis() = default;
    explicit Axis(const QString &n);
};

struct DataSet {
    int id = 0;
    Props p;
    QVector<QVector<double>> cols;  // столбцы: X, Y, ...
    QStringList strings;            // для xystring (и строк с подписями)
    QStringList extra;              // непонятые команды — сохраняются как есть

    DataSet();
    DataSet(int sid, const QString &type);

    QString type() const { return p.s("type"); }
    int ncols() const { return setTypeColumns(type()); }
    int size() const { return cols.isEmpty() ? 0 : cols[0].size(); }
    const QVector<double> &col(int i) const;
    void setType(const QString &t);  // меняет число столбцов
    void resize(int n);              // число строк
    QString displayName() const;
    bool hidden() const { return p.b("hidden"); }
};

struct Graph {
    int id = 0;
    Props hp;  // заголовочные "@g0 ..."
    Props p;
    Axis axes[4];
    QVector<DataSet> sets;
    QStringList extra;

    Graph();
    explicit Graph(int gid);

    Axis &axis(const QString &name);
    const Axis &axis(const QString &name) const;
    DataSet *set(int sid);
    const DataSet *set(int sid) const;
    DataSet &setOrCreate(int sid);
    int nextSetId() const;
    DataSet &newSet(const QString &type = "xy");
    bool visible() const { return hp.b("on") && !hp.b("hidden"); }
    QString type() const { return hp.s("type"); }
    QString displayName() const;
};

struct DrawObject {
    QString kind = "string";  // string line box ellipse
    Props p;
    int graph = 0;  // для loctype world
    DrawObject() = default;
    explicit DrawObject(const QString &k);
    QString displayName() const;
};

struct FontDef {
    int id;
    QString name;
    QString fallback;
};

struct Project {
    int version = 50125;
    int pageW = 792, pageH = 612;
    QStringList descriptions;
    QVector<FontDef> fonts;
    QMap<int, NamedColor> colors;
    int bgColor = 0;
    bool bgFill = true;
    QVector<Graph> graphs;
    QVector<DrawObject> objects;
    QStringList headerExtra;
    QString encoding = "latin-1";
    QString filename;
    QStringList warnings;

    Project();

    Graph *graph(int gid);
    const Graph *graph(int gid) const;
    Graph &graphOrCreate(int gid);
    int nextGraphId() const;

    QString fontName(int fid) const;
    QColor color(int idx) const;
    int addColor(const QColor &c, const QString &name = QString());
    void applyPalette(const QVector<NamedColor> &pal);
    QString paletteName() const;

    double scale() const { return double(qMin(pageW, pageH)); }
    double viewW() const { return pageW / scale(); }
    double viewH() const { return pageH / scale(); }
};

void defaultSetStyle(DataSet &s);

}  // namespace ng
