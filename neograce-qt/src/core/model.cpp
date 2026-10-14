#include "model.h"
#include "i18n.h"

#include "gracetext.h"

#include <algorithm>

namespace ng {

const QStringList kDefaultFonts = {
    "Times-Roman", "Times-Italic", "Times-Bold", "Times-BoldItalic",
    "Helvetica", "Helvetica-Oblique", "Helvetica-Bold", "Helvetica-BoldOblique",
    "Courier", "Courier-Oblique", "Courier-Bold", "Courier-BoldOblique",
    "Symbol", "ZapfDingbats",
};

const QVector<NamedColor> kClassicColors = {
    {QColor(255, 255, 255), "white"}, {QColor(0, 0, 0), "black"},
    {QColor(255, 0, 0), "red"},       {QColor(0, 255, 0), "green"},
    {QColor(0, 0, 255), "blue"},      {QColor(255, 255, 0), "yellow"},
    {QColor(188, 143, 143), "brown"}, {QColor(220, 220, 220), "grey"},
    {QColor(148, 0, 211), "violet"},  {QColor(0, 255, 255), "cyan"},
    {QColor(255, 0, 255), "magenta"}, {QColor(255, 165, 0), "orange"},
    {QColor(114, 33, 188), "indigo"}, {QColor(103, 7, 72), "maroon"},
    {QColor(64, 224, 208), "turquoise"}, {QColor(0, 139, 0), "green4"},
};

// Та же семантика (red остаётся красным), но спокойные различимые оттенки.
const QVector<NamedColor> kModernColors = {
    {QColor(255, 255, 255), "white"}, {QColor(0, 0, 0), "black"},
    {QColor(214, 39, 40), "red"},     {QColor(44, 160, 44), "green"},
    {QColor(31, 119, 180), "blue"},   {QColor(230, 171, 2), "yellow"},
    {QColor(140, 86, 75), "brown"},   {QColor(180, 180, 180), "grey"},
    {QColor(148, 103, 189), "violet"}, {QColor(23, 190, 207), "cyan"},
    {QColor(227, 119, 194), "magenta"}, {QColor(255, 127, 14), "orange"},
    {QColor(82, 84, 163), "indigo"},  {QColor(123, 65, 115), "maroon"},
    {QColor(64, 190, 176), "turquoise"}, {QColor(35, 120, 55), "green4"},
};

static QVector<NamedColor> namedPalette(const QVector<QColor> &c)
{
    static const QStringList names = {"white", "black", "red", "green", "blue", "yellow", "brown", "grey",
                                      "violet", "cyan", "magenta", "orange", "indigo", "maroon", "turquoise", "green4"};
    QVector<NamedColor> out;
    for (int i = 0; i < names.size(); ++i)
        out << NamedColor{i == 0 ? QColor(255, 255, 255) : i == 1 ? QColor(0, 0, 0) : c.value(i - 2), names[i]};
    return out;
}

const QVector<PaletteDef> &paletteDefs()
{
    static const QVector<PaletteDef> defs = {
        {"modern", "Современная", kModernColors},
        {"classic", "Классическая Grace", kClassicColors},
        // Okabe & Ito (2008) и Paul Tol — различимы при нарушениях цветового зрения
        {"okabe", "Okabe–Ito (для дальтоников)",
         namedPalette({{213, 94, 0}, {0, 158, 115}, {0, 114, 178}, {240, 228, 66}, {153, 102, 51}, {187, 187, 187},
                       {204, 121, 167}, {86, 180, 233}, {170, 68, 153}, {230, 159, 0}, {51, 34, 136}, {136, 34, 85},
                       {68, 170, 153}, {17, 119, 51}})},
        {"bright", "Яркая (для тёмного фона)",
         namedPalette({{255, 107, 107}, {105, 219, 124}, {77, 171, 247}, {255, 212, 59}, {214, 160, 120},
                       {173, 181, 189}, {177, 151, 252}, {102, 217, 232}, {240, 101, 149}, {255, 169, 77},
                       {116, 143, 252}, {230, 119, 163}, {56, 217, 169}, {64, 192, 87}})},
        {"pastel", "Пастельная",
         namedPalette({{232, 120, 120}, {110, 190, 130}, {110, 155, 210}, {235, 200, 95}, {190, 150, 130},
                       {190, 190, 190}, {175, 145, 205}, {115, 195, 205}, {220, 140, 195}, {242, 165, 100},
                       {135, 145, 205}, {175, 115, 145}, {110, 195, 175}, {85, 155, 105}})},
        {"deep", "Насыщенная тёмная",
         namedPalette({{178, 24, 43}, {27, 120, 55}, {33, 102, 172}, {191, 144, 0}, {140, 81, 10}, {150, 150, 150},
                       {118, 42, 131}, {1, 133, 113}, {197, 27, 125}, {217, 95, 2}, {49, 54, 149}, {103, 0, 31},
                       {53, 151, 143}, {0, 68, 27}})},
    };
    return defs;
}

const QVector<NamedColor> *paletteColors(const QString &key)
{
    for (const PaletteDef &d : paletteDefs())
        if (d.key == key)
            return &d.colors;
    return nullptr;
}

const QStringList kSetTypes = {
    "xy", "xydx", "xydy", "xydxdx", "xydydy", "xydxdy", "xydxdxdydy",
    "bar", "bardy", "bardydy", "xyhilo", "xyz", "xyr", "xysize",
    "xycolor", "xycolpat", "xyvmap", "xyboxplot", "xystring",
};

int setTypeColumns(const QString &t)
{
    static const QHash<QString, int> n = {
        {"xy", 2}, {"xydx", 3}, {"xydy", 3}, {"xydxdx", 4}, {"xydydy", 4},
        {"xydxdy", 4}, {"xydxdxdydy", 6}, {"bar", 2}, {"bardy", 3},
        {"bardydy", 4}, {"xyhilo", 5}, {"xyz", 3}, {"xyr", 3}, {"xysize", 3},
        {"xycolor", 3}, {"xycolpat", 4}, {"xyvmap", 4}, {"xyboxplot", 6},
        {"xystring", 2},
    };
    return n.value(t, 2);
}

QStringList setColumnNames(const QString &t)
{
    static const QHash<QString, QStringList> n = {
        {"xy", {"X", "Y"}},
        {"xydx", {"X", "Y", "dX"}},
        {"xydy", {"X", "Y", "dY"}},
        {"xydxdx", {"X", "Y", "dX+", "dX−"}},
        {"xydydy", {"X", "Y", "dY+", "dY−"}},
        {"xydxdy", {"X", "Y", "dX", "dY"}},
        {"xydxdxdydy", {"X", "Y", "dX+", "dX−", "dY+", "dY−"}},
        {"bar", {"X", "Y"}},
        {"bardy", {"X", "Y", "dY"}},
        {"bardydy", {"X", "Y", "dY+", "dY−"}},
        {"xyhilo", {"X", "High", "Low", "Open", "Close"}},
        {"xyz", {"X", "Y", "Z"}},
        {"xyr", {"X", "Y", "R"}},
        {"xysize", {"X", "Y", "Size"}},
        {"xycolor", {"X", "Y", "Color"}},
        {"xycolpat", {"X", "Y", "Color", "Pattern"}},
        {"xyvmap", {"X", "Y", "VX", "VY"}},
        {"xyboxplot", {"X", "Median", "Y1", "Y2", "Y3", "Y4"}},
        {"xystring", {"X", "Y"}},
    };
    return n.value(t, {"X", "Y"});
}

const QStringList kAxisNames = {"xaxis", "yaxis", "altxaxis", "altyaxis"};
const QStringList kPlacement = {"normal", "opposite", "both"};
const QStringList kTickLabelFormats = {
    "general", "decimal", "exponential", "power", "scientific", "engineering",
    "computing", "ddmmyy", "mmddyy", "yymmdd", "mmyy", "mmdd", "monthday",
    "daymonth", "months", "monthsy", "monthl", "dayofweek", "dayofweekl",
    "dayofyear", "hms", "mmddhms", "mmddyyhms", "yymmddhms", "degreeslon",
    "degreesmmlon", "degreesmmsslon", "mmsslon", "degreeslat", "degreesmmlat",
    "degreesmmsslat", "mmsslat",
};
const QStringList kScaleTypes = {"Normal", "Logarithmic", "Reciprocal", "Logit"};
const QStringList kGraphTypes = {"XY", "Chart", "Polar", "Smith", "Fixed", "Pie"};
const QString kGraphAxesAfter = "yinvert";

static PropDef D(const char *n, const char *kw, PT t, const QVariant &def,
                 const QStringList &ch = {})
{
    return PropDef{QString::fromLatin1(n), QString::fromLatin1(kw), t, def, ch};
}

const Schema &graphHeaderSchema()
{
    static const Schema s = {
        D("on", "", PT::OnOff, true),
        D("hidden", "hidden", PT::TF, false),
        D("type", "type", PT::Word, "XY", kGraphTypes),
        D("stacked", "stacked", PT::TF, false),
        D("bar_hgap", "bar hgap", PT::Float, 0.0),
        D("fixedpoint", "fixedpoint", PT::OnOff, false),
        D("fixedpoint_type", "fixedpoint type", PT::Int, 0),
        D("fixedpoint_xy", "fixedpoint xy", PT::Pair, makePair(0, 0)),
        D("fixedpoint_format", "fixedpoint format", PT::Rest, "general general"),
        D("fixedpoint_prec", "fixedpoint prec", PT::IntPair, makePair(6, 6)),
    };
    return s;
}

const Schema &graphSchema()
{
    static const Schema s = {
        D("world", "world", PT::QuadG, makeQuad(0, 0, 1, 1)),
        D("znorm", "znorm", PT::GFloat, 1.0),
        D("view", "view", PT::Quad, makeQuad(0.15, 0.15, 1.15, 0.85)),
        D("title", "title", PT::Str, ""),
        D("title_font", "title font", PT::Int, 0),
        D("title_size", "title size", PT::Float, 1.5),
        D("title_color", "title color", PT::Int, 1),
        D("subtitle", "subtitle", PT::Str, ""),
        D("subtitle_font", "subtitle font", PT::Int, 0),
        D("subtitle_size", "subtitle size", PT::Float, 1.0),
        D("subtitle_color", "subtitle color", PT::Int, 1),
        D("xscale", "xaxes scale", PT::Word, "Normal", kScaleTypes),
        D("yscale", "yaxes scale", PT::Word, "Normal", kScaleTypes),
        D("xinvert", "xaxes invert", PT::OnOff, false),
        D("yinvert", "yaxes invert", PT::OnOff, false),
        // --- здесь пишутся оси ---
        D("legend", "legend", PT::OnOff, true),
        D("legend_loctype", "legend loctype", PT::Word, "view", {"view", "world"}),
        D("legend_pos", "legend", PT::PairG, makePair(0.85, 0.8)),
        D("legend_box_color", "legend box color", PT::Int, 1),
        D("legend_box_pattern", "legend box pattern", PT::Int, 1),
        D("legend_box_linewidth", "legend box linewidth", PT::LW, 1.0),
        D("legend_box_linestyle", "legend box linestyle", PT::Int, 1),
        D("legend_box_fill_color", "legend box fill color", PT::Int, 0),
        D("legend_box_fill_pattern", "legend box fill pattern", PT::Int, 1),
        D("legend_font", "legend font", PT::Int, 0),
        D("legend_char_size", "legend char size", PT::Float, 1.0),
        D("legend_color", "legend color", PT::Int, 1),
        D("legend_length", "legend length", PT::Int, 4),
        D("legend_vgap", "legend vgap", PT::Int, 1),
        D("legend_hgap", "legend hgap", PT::Int, 1),
        D("legend_invert", "legend invert", PT::TF, false),
        D("frame_type", "frame type", PT::Int, 0),
        D("frame_linestyle", "frame linestyle", PT::Int, 1),
        D("frame_linewidth", "frame linewidth", PT::LW, 1.0),
        D("frame_color", "frame color", PT::Int, 1),
        D("frame_pattern", "frame pattern", PT::Int, 1),
        D("frame_bg_color", "frame background color", PT::Int, 0),
        D("frame_bg_pattern", "frame background pattern", PT::Int, 0),
    };
    return s;
}

const Schema &axisSchema()
{
    static const Schema s = {
        D("on", "", PT::OnOff, true),
        D("zero", "type zero", PT::TF, false),
        D("offset", "offset", PT::PairSp, makePair(0, 0)),
        D("bar", "bar", PT::OnOff, true),
        D("bar_color", "bar color", PT::Int, 1),
        D("bar_linestyle", "bar linestyle", PT::Int, 1),
        D("bar_linewidth", "bar linewidth", PT::LW, 1.0),
        D("label", "label", PT::Str, ""),
        D("label_layout", "label layout", PT::Word, "para", {"para", "perp"}),
        D("label_place_type", "label place", PT::Word, "auto", {"auto", "spec"}),
        D("label_place_xy", "label place", PT::Pair, makePair(0, 0.08)),
        D("label_char_size", "label char size", PT::Float, 1.0),
        D("label_font", "label font", PT::Int, 0),
        D("label_color", "label color", PT::Int, 1),
        D("label_side", "label place", PT::Word, "normal", kPlacement),
        D("tick", "tick", PT::OnOff, true),
        D("tick_major", "tick major", PT::GFloat, 0.5),
        D("tick_minor_ticks", "tick minor ticks", PT::Int, 1),
        D("tick_default", "tick default", PT::Int, 6),
        D("tick_rounded", "tick place rounded", PT::TF, true),
        D("tick_inout", "tick", PT::Word, "in", {"in", "out", "both"}),
        D("tick_major_size", "tick major size", PT::Float, 1.0),
        D("tick_major_color", "tick major color", PT::Int, 1),
        D("tick_major_linewidth", "tick major linewidth", PT::LW, 1.0),
        D("tick_major_linestyle", "tick major linestyle", PT::Int, 1),
        D("tick_major_grid", "tick major grid", PT::OnOff, false),
        D("tick_minor_color", "tick minor color", PT::Int, 1),
        D("tick_minor_linewidth", "tick minor linewidth", PT::LW, 1.0),
        D("tick_minor_linestyle", "tick minor linestyle", PT::Int, 1),
        D("tick_minor_grid", "tick minor grid", PT::OnOff, false),
        D("tick_minor_size", "tick minor size", PT::Float, 0.5),
        D("ticklabel", "ticklabel", PT::OnOff, true),
        D("ticklabel_format", "ticklabel format", PT::Word, "general", kTickLabelFormats),
        D("ticklabel_prec", "ticklabel prec", PT::Int, 5),
        D("ticklabel_formula", "ticklabel formula", PT::Str, ""),
        D("ticklabel_append", "ticklabel append", PT::Str, ""),
        D("ticklabel_prepend", "ticklabel prepend", PT::Str, ""),
        D("ticklabel_angle", "ticklabel angle", PT::Int, 0),
        D("ticklabel_skip", "ticklabel skip", PT::Int, 0),
        D("ticklabel_stagger", "ticklabel stagger", PT::Int, 0),
        D("ticklabel_place", "ticklabel place", PT::Word, "normal", kPlacement),
        D("ticklabel_offset_type", "ticklabel offset", PT::Word, "auto", {"auto", "spec"}),
        D("ticklabel_offset", "ticklabel offset", PT::PairSp, makePair(0, 0.01)),
        D("ticklabel_start_type", "ticklabel start type", PT::Word, "auto", {"auto", "spec"}),
        D("ticklabel_start", "ticklabel start", PT::Float, 0.0),
        D("ticklabel_stop_type", "ticklabel stop type", PT::Word, "auto", {"auto", "spec"}),
        D("ticklabel_stop", "ticklabel stop", PT::Float, 0.0),
        D("ticklabel_char_size", "ticklabel char size", PT::Float, 1.0),
        D("ticklabel_font", "ticklabel font", PT::Int, 0),
        D("ticklabel_color", "ticklabel color", PT::Int, 1),
        D("tick_place", "tick place", PT::Word, "both", kPlacement),
        D("tick_spec_type", "tick spec type", PT::Word, "none", {"none", "ticks", "both"}),
    };
    return s;
}

const Schema &setSchema()
{
    static const Schema s = {
        D("hidden", "hidden", PT::TF, false),
        D("type", "type", PT::Word, "xy", kSetTypes),
        D("symbol", "symbol", PT::Int, 0),
        D("symbol_size", "symbol size", PT::Float, 1.0),
        D("symbol_color", "symbol color", PT::Int, 1),
        D("symbol_pattern", "symbol pattern", PT::Int, 1),
        D("symbol_fill_color", "symbol fill color", PT::Int, 1),
        D("symbol_fill_pattern", "symbol fill pattern", PT::Int, 0),
        D("symbol_linewidth", "symbol linewidth", PT::LW, 1.0),
        D("symbol_linestyle", "symbol linestyle", PT::Int, 1),
        D("symbol_char", "symbol char", PT::Int, 65),
        D("symbol_char_font", "symbol char font", PT::Int, 0),
        D("symbol_skip", "symbol skip", PT::Int, 0),
        D("line_type", "line type", PT::Int, 1),
        D("line_linestyle", "line linestyle", PT::Int, 1),
        D("line_linewidth", "line linewidth", PT::LW, 1.0),
        D("line_color", "line color", PT::Int, 1),
        D("line_pattern", "line pattern", PT::Int, 1),
        D("baseline_type", "baseline type", PT::Int, 0),
        D("baseline", "baseline", PT::OnOff, false),
        D("dropline", "dropline", PT::OnOff, false),
        D("fill_type", "fill type", PT::Int, 0),
        D("fill_rule", "fill rule", PT::Int, 0),
        D("fill_color", "fill color", PT::Int, 1),
        D("fill_pattern", "fill pattern", PT::Int, 1),
        D("avalue", "avalue", PT::OnOff, false),
        D("avalue_type", "avalue type", PT::Int, 2),
        D("avalue_char_size", "avalue char size", PT::Float, 1.0),
        D("avalue_font", "avalue font", PT::Int, 0),
        D("avalue_color", "avalue color", PT::Int, 1),
        D("avalue_rot", "avalue rot", PT::Int, 0),
        D("avalue_format", "avalue format", PT::Word, "general", kTickLabelFormats),
        D("avalue_prec", "avalue prec", PT::Int, 3),
        D("avalue_prepend", "avalue prepend", PT::Str, ""),
        D("avalue_append", "avalue append", PT::Str, ""),
        D("avalue_offset", "avalue offset", PT::PairSp, makePair(0, 0)),
        D("errorbar", "errorbar", PT::OnOff, true),
        D("errorbar_place", "errorbar place", PT::Word, "both", kPlacement),
        D("errorbar_color", "errorbar color", PT::Int, 1),
        D("errorbar_pattern", "errorbar pattern", PT::Int, 1),
        D("errorbar_size", "errorbar size", PT::Float, 1.0),
        D("errorbar_linewidth", "errorbar linewidth", PT::LW, 1.0),
        D("errorbar_linestyle", "errorbar linestyle", PT::Int, 1),
        D("errorbar_riser_linewidth", "errorbar riser linewidth", PT::LW, 1.0),
        D("errorbar_riser_linestyle", "errorbar riser linestyle", PT::Int, 1),
        D("errorbar_riser_clip", "errorbar riser clip", PT::OnOff, false),
        D("errorbar_riser_clip_length", "errorbar riser clip length", PT::Float, 0.1),
        D("comment", "comment", PT::Str, ""),
        D("legend", "legend ", PT::Str, ""),  // Grace пишет два пробела
    };
    return s;
}

const Schema &objectSchema(const QString &kind)
{
    static const Schema str = {
        D("on", "on", PT::Flag, true),
        D("loctype", "loctype", PT::Word, "view", {"view", "world"}),
        D("pos", "", PT::PairG, makePair(0.5, 0.5)),
        D("color", "color", PT::Int, 1),
        D("rot", "rot", PT::Int, 0),
        D("font", "font", PT::Int, 0),
        D("just", "just", PT::Int, 0),
        D("char_size", "char size", PT::Float, 1.0),
        D("text", "def", PT::Str, ""),
    };
    static const Schema line = {
        D("on", "on", PT::Flag, true),
        D("loctype", "loctype", PT::Word, "view", {"view", "world"}),
        D("coords", "", PT::QuadG, makeQuad(0.2, 0.2, 0.4, 0.4)),
        D("linewidth", "linewidth", PT::LW, 1.0),
        D("linestyle", "linestyle", PT::Int, 1),
        D("color", "color", PT::Int, 1),
        D("arrow", "arrow", PT::Int, 0),
        D("arrow_type", "arrow type", PT::Int, 0),
        D("arrow_length", "arrow length", PT::Float, 1.0),
        D("arrow_layout", "arrow layout", PT::Pair, makePair(1, 1)),
    };
    static const Schema box = {
        D("on", "on", PT::Flag, true),
        D("loctype", "loctype", PT::Word, "view", {"view", "world"}),
        D("coords", "", PT::QuadG, makeQuad(0.2, 0.2, 0.4, 0.4)),
        D("linestyle", "linestyle", PT::Int, 1),
        D("linewidth", "linewidth", PT::LW, 1.0),
        D("color", "color", PT::Int, 1),
        D("fill_color", "fill color", PT::Int, 1),
        D("fill_pattern", "fill pattern", PT::Int, 0),
    };
    if (kind == "string")
        return str;
    if (kind == "line")
        return line;
    return box;  // box, ellipse
}

// ---- Axis / DataSet / Graph ---------------------------------------------------

Axis::Axis(const QString &n) : name(n), p(axisSchema())
{
    if (n.startsWith("alt"))
        p.set("on", false);
}

DataSet::DataSet() : DataSet(0, "xy") {}

DataSet::DataSet(int sid, const QString &type) : id(sid), p(setSchema())
{
    p.set("type", type);
    cols.resize(setTypeColumns(type));
}

const QVector<double> &DataSet::col(int i) const
{
    static const QVector<double> empty;
    if (i < 0 || i >= cols.size())
        return empty;
    return cols[i];
}

void DataSet::setType(const QString &t)
{
    const int n = setTypeColumns(t);
    const int rows = size();
    while (cols.size() < n)
        cols.append(QVector<double>(rows, 0.0));
    while (cols.size() > n)
        cols.removeLast();
    p.set("type", t);
}

void DataSet::resize(int n)
{
    for (auto &c : cols)
        c.resize(n);
    if (!strings.isEmpty()) {
        while (strings.size() < n)
            strings << QString();
        while (strings.size() > n)
            strings.removeLast();
    }
}

QString DataSet::displayName() const
{
    QString name = QString("S%1").arg(id);
    QString leg = p.s("legend");
    if (leg.isEmpty())
        leg = p.s("comment");
    if (!leg.isEmpty())
        name += " · " + stripEscapes(leg);
    return name;
}

Graph::Graph() : Graph(0) {}

Graph::Graph(int gid) : id(gid), hp(graphHeaderSchema()), p(graphSchema())
{
    for (int i = 0; i < 4; ++i)
        axes[i] = Axis(kAxisNames[i]);
}

Axis &Graph::axis(const QString &name)
{
    int i = kAxisNames.indexOf(name);
    return axes[i < 0 ? 0 : i];
}

const Axis &Graph::axis(const QString &name) const
{
    int i = kAxisNames.indexOf(name);
    return axes[i < 0 ? 0 : i];
}

DataSet *Graph::set(int sid)
{
    for (auto &s : sets)
        if (s.id == sid)
            return &s;
    return nullptr;
}

const DataSet *Graph::set(int sid) const
{
    for (const auto &s : sets)
        if (s.id == sid)
            return &s;
    return nullptr;
}

DataSet &Graph::setOrCreate(int sid)
{
    if (auto *s = set(sid))
        return *s;
    DataSet ns(sid, "xy");
    defaultSetStyle(ns);
    sets.append(ns);
    std::stable_sort(sets.begin(), sets.end(),
                     [](const DataSet &a, const DataSet &b) { return a.id < b.id; });
    return *set(sid);
}

int Graph::nextSetId() const
{
    int m = -1;
    for (const auto &s : sets)
        m = qMax(m, s.id);
    return m + 1;
}

DataSet &Graph::newSet(const QString &type)
{
    DataSet ns(nextSetId(), type);
    defaultSetStyle(ns);
    sets.append(ns);
    return sets.last();
}

QString Graph::displayName() const
{
    QString t = stripEscapes(p.s("title"));
    return QString("G%1").arg(id) + (t.isEmpty() ? QString() : " · " + t);
}

DrawObject::DrawObject(const QString &k) : kind(k), p(objectSchema(k)) {}

QString DrawObject::displayName() const
{
    static const QHash<QString, QString> names = {
        {"string", T("Текст")}, {"line", T("Линия")},
        {"box", T("Прямоугольник")}, {"ellipse", T("Эллипс")},
    };
    QString n = names.value(kind, kind);
    if (kind == "string") {
        QString t = stripEscapes(p.s("text")).replace('\n', ' ');
        if (!t.isEmpty())
            n += " · " + (t.size() > 28 ? t.left(28) + "…" : t);
    }
    return n;
}

// ---- Project -------------------------------------------------------------------

Project::Project()
{
    for (int i = 0; i < kDefaultFonts.size(); ++i)
        fonts.append({i, kDefaultFonts[i], kDefaultFonts[i]});
    for (int i = 0; i < kClassicColors.size(); ++i)
        colors[i] = kClassicColors[i];
}

Graph *Project::graph(int gid)
{
    for (auto &g : graphs)
        if (g.id == gid)
            return &g;
    return nullptr;
}

const Graph *Project::graph(int gid) const
{
    for (const auto &g : graphs)
        if (g.id == gid)
            return &g;
    return nullptr;
}

Graph &Project::graphOrCreate(int gid)
{
    if (auto *g = graph(gid))
        return *g;
    graphs.append(Graph(gid));
    std::stable_sort(graphs.begin(), graphs.end(),
                     [](const Graph &a, const Graph &b) { return a.id < b.id; });
    return *graph(gid);
}

int Project::nextGraphId() const
{
    int m = -1;
    for (const auto &g : graphs)
        m = qMax(m, g.id);
    return m + 1;
}

QString Project::fontName(int fid) const
{
    for (const auto &f : fonts)
        if (f.id == fid)
            return f.name;
    if (fid >= 0 && fid < kDefaultFonts.size())
        return kDefaultFonts[fid];
    return "Times-Roman";
}

QColor Project::color(int idx) const
{
    auto it = colors.find(idx);
    if (it == colors.end())
        return QColor(0, 0, 0);
    return it->rgb;
}

int Project::addColor(const QColor &c, const QString &name)
{
    for (auto it = colors.begin(); it != colors.end(); ++it)
        if (it->rgb.rgb() == c.rgb())
            return it.key();
    int i = colors.isEmpty() ? 0 : colors.lastKey() + 1;
    colors[i] = NamedColor{c, name.isEmpty() ? QString("color%1").arg(i) : name};
    return i;
}

void Project::applyPalette(const QVector<NamedColor> &pal)
{
    for (int i = 0; i < pal.size(); ++i)
        colors[i] = pal[i];
}

QString Project::paletteName() const
{
    auto same = [this](const QVector<NamedColor> &pal) {
        for (int i = 0; i < pal.size(); ++i) {
            auto it = colors.find(i);
            if (it == colors.end() || it->rgb.rgb() != pal[i].rgb.rgb())
                return false;
        }
        return true;
    };
    for (const PaletteDef &d : paletteDefs())
        if (same(d.colors))
            return d.key;
    return "custom";
}

void defaultSetStyle(DataSet &s)
{
    // хорошо различимая последовательность (в .agr цвета всегда заданы явно,
    // так что это касается только новых наборов и файлов данных)
    static const int cycle[] = {4, 2, 3, 11, 8, 9, 10, 6, 12, 14, 13, 15, 1, 5, 7};
    const int c = cycle[s.id % 15];
    s.p.set("line_linewidth", 1.5);
    s.p.set("line_color", c);
    s.p.set("symbol_color", c);
    s.p.set("symbol_fill_color", c);
    s.p.set("fill_color", c);
    s.p.set("errorbar_color", c);
}

}  // namespace ng
