#include "inspector.h"

#include "gracetext.h"
#include "i18n.h"
#include "icons.h"
#include "ops.h"
#include "widgets.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <cmath>

namespace ng {

struct Inspector::Field {
    enum Ed { Auto, Color, Pattern, LineStyle, Symbol, Font, Choice, Check, Spin, DSpin, Text, Pair, Quad, Width };
    QString name, label;
    Ed ed = Auto;
    QStringList labels;
    QVariantList values;
    double min = -1e12, max = 1e12, step = 0.1;
    int decimals = 4;
    QStringList sub;
    QString tip;
};
using F = Inspector::Field;

static F fld(const QString &n, const QString &l, F::Ed ed = F::Auto)
{
    F f;
    f.name = n;
    f.label = l;
    f.ed = ed;
    return f;
}
static F choice(const QString &n, const QString &l, const QVariantList &vals, const QStringList &labels)
{
    F f = fld(n, l, F::Choice);
    f.values = vals;
    f.labels = labels;
    return f;
}
static F num(const QString &n, const QString &l, double mn, double mx, double step, int dec = 2)
{
    F f = fld(n, l, F::DSpin);
    f.min = mn;
    f.max = mx;
    f.step = step;
    f.decimals = dec;
    return f;
}
static F ispin(const QString &n, const QString &l, int mn, int mx)
{
    F f = fld(n, l, F::Spin);
    f.min = mn;
    f.max = mx;
    return f;
}
static F pair(const QString &n, const QString &l, const QStringList &sub, int dec = 4)
{
    F f = fld(n, l, F::Pair);
    f.sub = sub;
    f.decimals = dec;
    return f;
}
static F quad(const QString &n, const QString &l, const QStringList &sub, int dec = 6)
{
    F f = fld(n, l, F::Quad);
    f.sub = sub;
    f.decimals = dec;
    return f;
}
static F tip(F f, const QString &t)
{
    f.tip = t;
    return f;
}

// ---- списки значений
static const QVariantList kPlaceV = {"normal", "opposite", "both"};
static const QStringList kPlaceL = {T("Обычно"), T("Напротив"), T("С обеих сторон")};
static const QVariantList kAutoV = {"auto", "spec"};
static const QStringList kAutoL = {T("Авто"), T("Задать")};

static QVariantList ints(int n)
{
    QVariantList v;
    for (int i = 0; i < n; ++i)
        v << i;
    return v;
}

static const QString kMarkupTip = T("Разметка Grace: \\S верхний индекс, \\s нижний, \\N обычный; \\x греческий (\\xa = α); " "\\f{} исходный шрифт; \\+ \\- крупнее/мельче; \\u…\\U подчёркивание; \\o…\\O черта сверху. " "Греческие буквы можно вводить с клавиатуры.");

static QString wordLabel(const QString &w)
{
    static const QHash<QString, QString> t = {
        {"general", T("Общий")}, {"decimal", T("Десятичный")}, {"exponential", T("Экспонента (1e3)")},
        {"power", T("Степень 10ⁿ")}, {"scientific", T("Научный (a×10ⁿ)")}, {"engineering", T("Инженерный (k, M…)")},
        {"computing", T("Компьютерный (K, M по 1024)")}, {"ddmmyy", T("ДД-ММ-ГГГГ")}, {"mmddyy", T("ММ-ДД-ГГГГ")},
        {"yymmdd", T("ГГГГ-ММ-ДД")}, {"hms", T("чч:мм:сс")}, {"degreeslon", T("Градусы (долгота)")},
        {"degreeslat", T("Градусы (широта)")}, {"months", T("Месяц")}, {"dayofweek", T("День недели")},
    };
    return t.value(w, w);
}

// ---------------------------------------------------------------------------

Inspector::Inspector(Document *doc, QWidget *parent) : QScrollArea(parent), m_doc(doc)
{
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setMinimumWidth(300);
    connect(doc, &Document::selectionChanged, this, &Inspector::rebuild);
    connect(doc, &Document::changed, this, [this] {
        // структура могла измениться (тип набора, число осей…)
        const Sel s = m_doc->selection();
        bool exists = true;
        if (s.kind == Sel::Set)
            exists = m_doc->project().graph(s.gid) && m_doc->project().graph(s.gid)->set(s.sid);
        if (s.kind == Sel::Object)
            exists = s.obj >= 0 && s.obj < m_doc->project().objects.size();
        if (!exists)
            rebuild();
        else
            refresh();
    });
    rebuild();
}

void Inspector::refresh()
{
    m_refreshing = true;
    for (auto &f : m_refreshers)
        f();
    m_refreshing = false;
}

Section *Inspector::section(const QString &title, bool expanded)
{
    auto *s = new Section(title, m_host, expanded);
    m_lay->addWidget(s);
    return s;
}

void Inspector::header(const QString &title, const QString &subtitle, const QString &iconName)
{
    auto *w = new QWidget(m_host);
    auto *h = new QHBoxLayout(w);
    h->setContentsMargins(12, 12, 12, 8);
    auto *ic = new QLabel(w);
    ic->setPixmap(icon(iconName).pixmap(28, 28));
    h->addWidget(ic);
    auto *v = new QVBoxLayout();
    v->setSpacing(0);
    auto *t = new QLabel(title, w);
    QFont f = t->font();
    f.setPointSizeF(f.pointSizeF() * 1.25);
    f.setWeight(QFont::DemiBold);
    t->setFont(f);
    v->addWidget(t);
    if (!subtitle.isEmpty()) {
        auto *st = new QLabel(subtitle, w);
        st->setObjectName("subtle");
        st->setWordWrap(true);
        v->addWidget(st);
    }
    h->addLayout(v, 1);
    m_lay->addWidget(w);
}

static const Schema &schemaFor(const Project &pr, const PropTarget &t)
{
    switch (t.kind) {
    case PropTarget::GraphHeader: return graphHeaderSchema();
    case PropTarget::GraphP: return graphSchema();
    case PropTarget::AxisP: return axisSchema();
    case PropTarget::SetP: return setSchema();
    case PropTarget::ObjectP: return objectSchema(pr.objects.value(t.obj).kind);
    }
    return graphSchema();
}

void Inspector::addFields(Section *sec, const PropTarget &t, const QList<Field> &fields)
{
    QFormLayout *form = sec->form();
    for (Field f : fields) {
        const PropDef *def = findDef(schemaFor(m_doc->project(), t), f.name);
        if (!def)
            continue;
        if (f.ed == F::Auto) {
            const QString n = f.name;
            switch (def->type) {
            case PT::OnOff: case PT::TF: case PT::Flag: f.ed = F::Check; break;
            case PT::Int:
                if (n.endsWith("color")) f.ed = F::Color;
                else if (n.endsWith("pattern")) f.ed = F::Pattern;
                else if (n.endsWith("linestyle")) f.ed = F::LineStyle;
                else if (n.endsWith("font")) f.ed = F::Font;
                else if (n == "symbol") f.ed = F::Symbol;
                else f.ed = F::Spin;
                break;
            case PT::Float: case PT::GFloat: f.ed = F::DSpin; break;
            case PT::LW: f.ed = F::Width; break;
            case PT::Str: f.ed = F::Text; break;
            case PT::Word:
                f.ed = F::Choice;
                if (f.values.isEmpty()) {
                    for (const auto &c : def->choices) {
                        f.values << c;
                        f.labels << wordLabel(c);
                    }
                }
                break;
            case PT::Pair: case PT::PairG: case PT::PairSp: case PT::IntPair: f.ed = F::Pair; break;
            case PT::Quad: case PT::QuadG: f.ed = F::Quad; break;
            default: f.ed = F::Text;
            }
        }
        const QString name = f.name;
        const QString label = f.label;
        auto get = [this, t, name]() -> QVariant {
            Props *p = resolveProps(const_cast<Project &>(m_doc->project()), t);
            return p ? p->get(name) : QVariant();
        };
        auto set = [this, t, name, label](const QVariant &v) {
            if (m_refreshing)
                return;
            // групповая правка: свойство набора меняется у всех выделенных
            if (t.kind == PropTarget::SetP && m_doc->selectedSets().size() > 1
                && m_doc->isSetSelected(t.gid, t.sid)) {
                m_doc->setPropMany(m_doc->selectedSetTargets(), name, v, T("Изменение: %1").arg(label));
                return;
            }
            m_doc->setProp(t, name, v, T("Изменение: %1").arg(label));
        };
        QWidget *w = nullptr;
        switch (f.ed) {
        case F::Check: {
            auto *cb = new QCheckBox(f.label, sec);
            connect(cb, &QCheckBox::toggled, this, [set](bool on) { set(on); });
            m_refreshers << [cb, get] {
                QSignalBlocker b(cb);
                cb->setChecked(get().toBool());
            };
            form->addRow(QString(), cb);
            if (!f.tip.isEmpty())
                cb->setToolTip(f.tip);
            continue;
        }
        case F::Color: {
            auto *b = new ColorButton(m_doc, sec);
            connect(b, &ColorButton::picked, this, [set](int i) { set(i); });
            m_refreshers << [b, get] { b->setIndex(get().toInt()); };
            w = b;
            break;
        }
        case F::Pattern: case F::LineStyle: case F::Symbol: case F::Font: case F::Choice: {
            auto *cb = new QComboBox(sec);
            cb->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
            cb->setMinimumContentsLength(4);
            const QColor tc = palette().color(QPalette::Text);
            if (f.ed == F::Pattern) {
                cb->addItem(patternIcon(0, tc), T("Нет"), 0);
                cb->addItem(patternIcon(1, tc), T("Сплошная"), 1);
                for (int i = 2; i < 32; ++i)
                    cb->addItem(patternIcon(i, tc), QString(T("Узор %1")).arg(i), i);
                cb->setIconSize(QSize(24, 14));
            } else if (f.ed == F::LineStyle) {
                static const QStringList names = {T("Нет"), T("Сплошная"), T("Точки"), T("Штрихи"), T("Длинные штрихи"),
                                                  T("Точка-штрих"), T("Точка-длинный штрих"), T("Точка-штрих-точка"),
                                                  T("Штрих-точка-штрих")};
                for (int i = 0; i < names.size(); ++i)
                    cb->addItem(lineStyleIcon(i, tc), names[i], i);
                cb->setIconSize(QSize(48, 14));
            } else if (f.ed == F::Symbol) {
                static const QStringList names = {T("Нет"), T("Круг"), T("Квадрат"), T("Ромб"), T("Треугольник ▲"), T("Треугольник ◀"),
                                                  T("Треугольник ▼"), T("Треугольник ▶"), T("Плюс"), T("Крест"), T("Звезда"), T("Знак")};
                for (int i = 0; i < names.size(); ++i)
                    cb->addItem(symbolIcon(i, tc), names[i], i);
            } else if (f.ed == F::Font) {
                for (const auto &fd : m_doc->project().fonts)
                    cb->addItem(QString("%1  %2").arg(fd.id).arg(fd.name), fd.id);
            } else {
                for (int i = 0; i < f.values.size(); ++i)
                    cb->addItem(f.labels.value(i, f.values[i].toString()), f.values[i]);
            }
            connect(cb, QOverload<int>::of(&QComboBox::activated), this, [cb, set](int i) { set(cb->itemData(i)); });
            m_refreshers << [cb, get] {
                QSignalBlocker b(cb);
                const QVariant v = get();
                int idx = -1;
                for (int i = 0; i < cb->count(); ++i) {
                    const QVariant d = cb->itemData(i);
                    if ((d.typeId() == QMetaType::QString && d.toString() == v.toString())
                        || (d.typeId() != QMetaType::QString && d.toInt() == v.toInt())) {
                        idx = i;
                        break;
                    }
                }
                if (idx < 0 && v.isValid()) {
                    cb->addItem(v.toString(), v);
                    idx = cb->count() - 1;
                }
                cb->setCurrentIndex(idx);
            };
            w = cb;
            break;
        }
        case F::Spin: {
            auto *sb = new QSpinBox(sec);
            sb->setMinimumWidth(40);
            sb->setRange(int(qMax(-1e9, f.min)), int(qMin(1e9, f.max)));
            sb->setKeyboardTracking(false);
            connect(sb, QOverload<int>::of(&QSpinBox::valueChanged), this, [set](int v) { set(v); });
            m_refreshers << [sb, get] {
                QSignalBlocker b(sb);
                sb->setValue(get().toInt());
            };
            w = sb;
            break;
        }
        case F::DSpin: case F::Width: {
            auto *sb = new GSpinBox(sec);
            if (f.ed == F::Width) {
                sb->setRange(0, 50);
                sb->setSingleStep(0.5);
            } else {
                sb->setRange(f.min, f.max);
                sb->setSingleStep(f.step);
            }
            sb->setKeyboardTracking(false);
            connect(sb, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [set](double v) { set(v); });
            m_refreshers << [sb, get] {
                QSignalBlocker b(sb);
                sb->setValue(get().toDouble());
            };
            w = sb;
            break;
        }
        case F::Text: {
            auto *le = new QLineEdit(sec);
            le->setClearButtonEnabled(true);
            le->setToolTip(f.tip.isEmpty() ? kMarkupTip : f.tip);
            connect(le, &QLineEdit::editingFinished, this, [le, set] { set(unicodeToGrace(le->text())); });
            m_refreshers << [le, get] {
                if (le->hasFocus())
                    return;
                QSignalBlocker b(le);
                le->setText(get().toString());
                le->setCursorPosition(0);
            };
            w = le;
            break;
        }
        case F::Pair: case F::Quad: {
            const int n = f.ed == F::Pair ? 2 : 4;
            auto *box = new QWidget(sec);
            auto *g = new QGridLayout(box);
            g->setContentsMargins(0, 0, 0, 0);
            g->setHorizontalSpacing(4);
            g->setVerticalSpacing(3);
            QVector<GSpinBox *> spins;
            for (int k = 0; k < n; ++k) {
                auto *sb = new GSpinBox(box);
                sb->setSingleStep(0.01);
                sb->setKeyboardTracking(false);
                sb->setButtonSymbols(QAbstractSpinBox::NoButtons);
                sb->setToolTip(f.sub.value(k));
                auto *lab = new QLabel(f.sub.value(k), box);
                lab->setObjectName("subtle");
                const int row = n == 4 ? k / 2 : 0, col = n == 4 ? (k % 2) * 2 : k * 2;
                g->addWidget(lab, row, col);
                g->addWidget(sb, row, col + 1);
                spins << sb;
            }
            for (auto *sb : spins)
                connect(sb, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [spins, set, n] {
                    QList<double> l;
                    for (auto *s : spins)
                        l << s->value();
                    set(n == 2 ? makePair(l[0], l[1]) : makeQuad(l[0], l[1], l[2], l[3]));
                });
            m_refreshers << [spins, get] {
                auto l = get().value<QList<double>>();
                for (int k = 0; k < spins.size(); ++k) {
                    QSignalBlocker b(spins[k]);
                    spins[k]->setValue(l.value(k));
                }
            };
            w = box;
            break;
        }
        default:
            break;
        }
        if (!w)
            continue;
        w->setMinimumWidth(40);
        if (!m_editors.contains(f.name))
            m_editors.insert(f.name, w);
        if (auto *le = qobject_cast<QLineEdit *>(w))
            le->setMinimumWidth(40);
        if (!f.tip.isEmpty())
            w->setToolTip(f.tip);
        form->addRow(f.label, w);
    }
}

void Inspector::focusProperty(const QString &name)
{
    QWidget *w = m_editors.value(name);
    if (!w)
        return;
    ensureWidgetVisible(w, 20, 60);
    w->setFocus(Qt::OtherFocusReason);
    if (auto *le = qobject_cast<QLineEdit *>(w))
        le->selectAll();
}

void Inspector::rebuild()
{
    m_refreshers.clear();
    m_editors.clear();
    m_host = new QWidget();
    m_host->setObjectName("inspectorHost");
    m_host->setMinimumWidth(10);
    m_lay = new QVBoxLayout(m_host);
    m_lay->setContentsMargins(0, 0, 0, 12);
    m_lay->setSpacing(2);
    const Sel s = m_doc->selection();
    m_built = s;
    switch (s.kind) {
    case Sel::Graph: buildGraph(s.gid); break;
    case Sel::Legend: buildGraph(s.gid, true); break;
    case Sel::Title: buildGraph(s.gid, false, true); break;
    case Sel::Axis: buildAxis(s.gid, s.axis); break;
    case Sel::Set: buildSet(s.gid, s.sid); break;
    case Sel::Object: buildObject(s.obj); break;
    default: buildPage(); break;
    }
    m_lay->addStretch(1);
    setWidget(m_host);  // старый виджет удаляется
    refresh();
}

// ---- страница

void Inspector::buildPage()
{
    const Project &pr = m_doc->project();
    header(T("Страница"), pr.filename.isEmpty() ? T("Новый проект") : pr.filename, "page");
    Section *sz = section(T("Размер"));
    auto *preset = new QComboBox(sz);
    struct P { const char *name; int w, h; };
    static const P presets[] = {{"Letter, альбомная (792×612)", 792, 612}, {"Letter, книжная (612×792)", 612, 792},
                                {"A4, альбомная (842×595)", 842, 595},   {"A4, книжная (595×842)", 595, 842},
                                {"16:9 (960×540)", 960, 540},            {"4:3 (800×600)", 800, 600},
                                {"Квадрат (600×600)", 600, 600},        {"Статья, 1 колонка (252×189)", 252, 189},
                                {"Статья, 2 колонки (504×311)", 504, 311}};
    preset->addItem(T("Свой размер"));
    for (const auto &p : presets)
        preset->addItem(T(p.name), QSize(p.w, p.h));
    sz->form()->addRow(T("Шаблон"), preset);
    auto *w = new QSpinBox(sz);
    auto *h = new QSpinBox(sz);
    for (auto *sb : {w, h}) {
        sb->setRange(50, 20000);
        sb->setSuffix(QStringLiteral(" pt"));
        sb->setKeyboardTracking(false);
    }
    sz->form()->addRow(T("Ширина"), w);
    sz->form()->addRow(T("Высота"), h);
    auto *keep = new QCheckBox(T("Сохранять положение графиков"), sz);
    keep->setChecked(true);
    keep->setToolTip(T("Масштабировать поля графиков вместе со страницей"));
    sz->form()->addRow(QString(), keep);
    auto apply = [this, keep](int nw, int nh) {
        const Project &p0 = m_doc->project();
        if (nw == p0.pageW && nh == p0.pageH)
            return;
        m_doc->modify(T("Размер страницы"), [&](Project &pr) {
            const double ow = pr.viewW(), oh = pr.viewH();
            pr.pageW = nw;
            pr.pageH = nh;
            if (keep->isChecked()) {
                const double sx = pr.viewW() / ow, sy = pr.viewH() / oh;
                for (Graph &g : pr.graphs) {
                    Vec4 v = g.p.v4("view");
                    g.p.setV4("view", v[0] * sx, v[1] * sy, v[2] * sx, v[3] * sy);
                    if (g.p.s("legend_loctype") == "view") {
                        Vec2 l = g.p.v2("legend_pos");
                        g.p.setV2("legend_pos", l[0] * sx, l[1] * sy);
                    }
                }
                for (DrawObject &o : pr.objects) {
                    if (o.p.s("loctype") != "view")
                        continue;
                    if (o.kind == "string") {
                        Vec2 p = o.p.v2("pos");
                        o.p.setV2("pos", p[0] * sx, p[1] * sy);
                    } else {
                        Vec4 c = o.p.v4("coords");
                        o.p.setV4("coords", c[0] * sx, c[1] * sy, c[2] * sx, c[3] * sy);
                    }
                }
            }
        });
    };
    connect(preset, QOverload<int>::of(&QComboBox::activated), this, [preset, apply](int i) {
        const QSize s = preset->itemData(i).toSize();
        if (s.isValid())
            apply(s.width(), s.height());
    });
    connect(w, QOverload<int>::of(&QSpinBox::valueChanged), this, [this, apply](int v) {
        if (!m_refreshing) apply(v, m_doc->project().pageH);
    });
    connect(h, QOverload<int>::of(&QSpinBox::valueChanged), this, [this, apply](int v) {
        if (!m_refreshing) apply(m_doc->project().pageW, v);
    });
    m_refreshers << [this, w, h, preset] {
        const Project &p = m_doc->project();
        QSignalBlocker b1(w), b2(h), b3(preset);
        w->setValue(p.pageW);
        h->setValue(p.pageH);
        int idx = 0;
        for (int i = 1; i < preset->count(); ++i)
            if (preset->itemData(i).toSize() == QSize(p.pageW, p.pageH))
                idx = i;
        preset->setCurrentIndex(idx);
    };

    Section *bg = section(T("Фон"));
    auto *fill = new QCheckBox(T("Заливать фон страницы"), bg);
    auto *cb = new ColorButton(m_doc, bg);
    bg->form()->addRow(QString(), fill);
    bg->form()->addRow(T("Цвет"), cb);
    connect(fill, &QCheckBox::toggled, this, [this](bool on) {
        if (m_refreshing) return;
        m_doc->modify(T("Фон страницы"), [on](Project &pr) { pr.bgFill = on; });
    });
    connect(cb, &ColorButton::picked, this, [this](int i) {
        m_doc->modify(T("Цвет фона"), [i](Project &pr) { pr.bgColor = i; });
    });
    m_refreshers << [this, fill, cb] {
        QSignalBlocker b(fill);
        fill->setChecked(m_doc->project().bgFill);
        cb->setIndex(m_doc->project().bgColor);
    };

    Section *st = section(T("Оформление"));
    auto *style = new QComboBox(st);
    style->addItem(T("Выберите стиль…"), QString());
    for (const StyleDef &sd : styleDefs()) {
        style->addItem(T(sd.name), sd.key);
        style->setItemData(style->count() - 1, T(sd.tip), Qt::ToolTipRole);
    }
    st->form()->addRow(T("Стиль"), style);
    connect(style, QOverload<int>::of(&QComboBox::activated), this, [this, style](int i) {
        const QString key = style->itemData(i).toString();
        if (key.isEmpty())
            return;
        m_doc->modify(T("Стиль: %1").arg(style->itemText(i)), [key](Project &pr) { applyStyle(pr, key); });
        QSignalBlocker b(style);
        style->setCurrentIndex(0);
    });
    auto *gal = new QPushButton(icon("palette"), T("Галерея стилей…"), st);
    connect(gal, &QPushButton::clicked, this, &Inspector::styleGalleryWanted);
    st->body()->addWidget(gal);
    auto *pal = new QComboBox(st);
    for (const PaletteDef &pd : paletteDefs())
        pal->addItem(T(pd.name), pd.key);
    pal->addItem(T("Своя"), "custom");
    st->form()->addRow(T("Цвета"), pal);
    connect(pal, QOverload<int>::of(&QComboBox::activated), this, [this, pal](int i) {
        const QString k = pal->itemData(i).toString();
        if (k == "custom") return;
        m_doc->modify(T("Палитра"), [k](Project &pr) {
            if (const auto *c = paletteColors(k))
                pr.applyPalette(*c);
        });
    });
    m_refreshers << [this, pal] {
        QSignalBlocker b(pal);
        pal->setCurrentIndex(pal->findData(m_doc->project().paletteName()));
    };
    auto *arr = new QPushButton(icon("arrange"), T("Расставить графики…"), st);
    connect(arr, &QPushButton::clicked, this, &Inspector::arrangeWanted);
    st->body()->addWidget(arr);

    Section *ds = section(T("Описание проекта"), false);
    auto *te = new QPlainTextEdit(ds);
    te->setMaximumHeight(90);
    ds->body()->addWidget(te);
    auto *save = new QTimer(te);
    save->setSingleShot(true);
    save->setInterval(600);
    connect(te, &QPlainTextEdit::textChanged, save, qOverload<>(&QTimer::start));
    connect(save, &QTimer::timeout, this, [this, te] {
        const QStringList lines = te->toPlainText().split('\n');
        if (lines == m_doc->project().descriptions) return;
        m_doc->modify(T("Описание"), [&](Project &pr) { pr.descriptions = te->toPlainText().isEmpty() ? QStringList() : lines; });
    });
    m_refreshers << [this, te] {
        if (te->hasFocus()) return;
        QSignalBlocker b(te);
        te->setPlainText(m_doc->project().descriptions.join('\n'));
    };
}

// ---- график

void Inspector::buildGraph(int gid, bool legendOnly, bool titleOnly)
{
    const Graph *g = m_doc->project().graph(gid);
    if (!g) {
        buildPage();
        return;
    }
    PropTarget hp{PropTarget::GraphHeader, gid};
    PropTarget gp{PropTarget::GraphP, gid};
    if (legendOnly) {
        header(T("Легенда"), T("График G%1 · перетащите мышью, чтобы переместить").arg(gid), "legend");
    } else if (titleOnly) {
        header(T("Заголовки"), T("График G%1 · двойной щелчок — свойства").arg(gid), "title");
    } else {
        header(T("График G%1").arg(gid), stripEscapes(g->p.s("title")), "graph");
    }

    if (!legendOnly && !titleOnly) {
        Section *m = section(T("Тип"));
        addFields(m, hp, {choice("type", T("Тип графика"), {"XY", "Chart", "Polar", "Smith", "Fixed", "Pie"},
                                 {"XY", T("Диаграмма (Chart)"), T("Полярный"), T("Смита"), T("Фиксированный"), T("Круговой")}),
                          fld("stacked", T("Накопление (Chart)")),
                          num("bar_hgap", T("Промежуток между барами"), 0, 1, 0.005, 3)});
        addFields(m, gp, {tip(num("znorm", T("Нормировка Z"), -1e9, 1e9, 0.1, 4),
                              T("Масштаб для размеров символов (xysize) и векторов (xyvmap)"))});

        Section *sc = section(T("Масштаб"));
        addFields(sc, gp, {quad("world", T("Границы"), {T("X от"), T("Y от"), T("X до"), T("Y до")}, 8),
                           choice("xscale", T("Шкала X"), {"Normal", "Logarithmic", "Reciprocal", "Logit"},
                                  {T("Линейная"), T("Логарифмическая"), T("Обратная 1/x"), T("Логит")}),
                           choice("yscale", T("Шкала Y"), {"Normal", "Logarithmic", "Reciprocal", "Logit"},
                                  {T("Линейная"), T("Логарифмическая"), T("Обратная 1/x"), T("Логит")}),
                           fld("xinvert", T("Обратить ось X")), fld("yinvert", T("Обратить ось Y"))});
        auto *btns = new QWidget(sc);
        auto *bl = new QHBoxLayout(btns);
        bl->setContentsMargins(0, 0, 0, 0);
        auto *as = new QPushButton(icon("autoscale"), T("Автомасштаб"), btns);
        connect(as, &QPushButton::clicked, this, [this, gid] { emit autoscaleWanted(gid); });
        auto *tk = new QPushButton(T("Подобрать деления"), btns);
        connect(tk, &QPushButton::clicked, this, [this, gid] {
            m_doc->modify(T("Деления"), [gid](Project &pr) {
                if (Graph *gg = pr.graph(gid)) updateTicksForWorld(*gg);
            });
        });
        bl->addWidget(as);
        bl->addWidget(tk);
        sc->body()->addWidget(btns);

        Section *ax = section(T("Оси"));
        auto *axw = new QWidget(ax);
        auto *axl = new QGridLayout(axw);
        axl->setContentsMargins(0, 0, 0, 0);
        static const char *an[] = {"Ось X", "Ось Y", "Верхняя ось X", "Правая ось Y"};
        for (int a = 0; a < 4; ++a) {
            auto *b = new QPushButton(icon("axis"), T(an[a]), axw);
            connect(b, &QPushButton::clicked, this, [this, gid, a] { m_doc->select(Sel::axisOf(gid, a)); });
            axl->addWidget(b, a / 2, a % 2);
        }
        ax->body()->addWidget(axw);
    }

    if (!legendOnly) {
        Section *t = section(T("Заголовок"), true);
        addFields(t, gp, {fld("title", T("Текст")), fld("title_font", T("Шрифт")), num("title_size", T("Размер"), 0, 20, 0.1),
                          fld("title_color", T("Цвет"))});
        Section *st = section(T("Подзаголовок"), titleOnly);
        addFields(st, gp, {fld("subtitle", T("Текст")), fld("subtitle_font", T("Шрифт")),
                           num("subtitle_size", T("Размер"), 0, 20, 0.1), fld("subtitle_color", T("Цвет"))});
    }
    if (titleOnly)
        return;

    Section *lg = section(T("Легенда"), legendOnly);
    addFields(lg, gp, {fld("legend", T("Показывать легенду")),
                       choice("legend_loctype", T("Координаты"), {"view", "world"}, {T("Страница"), T("Как у графика")}),
                       pair("legend_pos", T("Левый верхний угол"), {"x", "y"}), fld("legend_font", T("Шрифт")),
                       num("legend_char_size", T("Размер текста"), 0, 20, 0.05), fld("legend_color", T("Цвет текста")),
                       ispin("legend_length", T("Длина линии"), 0, 20), ispin("legend_vgap", T("Интервал по вертикали"), 0, 20),
                       ispin("legend_hgap", T("Отступ по горизонтали"), 0, 20), fld("legend_invert", T("Обратный порядок")),
                       fld("legend_box_color", T("Цвет рамки")), fld("legend_box_pattern", T("Рамка")),
                       fld("legend_box_linewidth", T("Толщина рамки")), fld("legend_box_linestyle", T("Стиль рамки")),
                       fld("legend_box_fill_color", T("Цвет фона")), fld("legend_box_fill_pattern", T("Фон"))});
    if (legendOnly)
        return;

    Section *pos = section(T("Положение на странице"), false);
    addFields(pos, gp, {quad("view", T("Область"), {"x₁", "y₁", "x₂", "y₂"}, 4)});
    auto *hint = new QLabel(T("Единица — короткая сторона страницы. Выделите график и тяните за ручки мышью."), pos);
    hint->setWordWrap(true);
    hint->setObjectName("subtle");
    pos->body()->addWidget(hint);

    Section *fr = section(T("Рамка и фон"), false);
    addFields(fr, gp, {choice("frame_type", T("Рамка"), ints(6),
                              {T("Замкнутая"), T("Открытая (L)"), T("Без верха"), T("Без низа"), T("Без левой"), T("Без правой")}),
                       fld("frame_color", T("Цвет")), fld("frame_pattern", T("Узор линии")),
                       fld("frame_linewidth", T("Толщина")), fld("frame_linestyle", T("Стиль")),
                       fld("frame_bg_color", T("Цвет фона")), fld("frame_bg_pattern", T("Фон"))});
}

// ---- ось

void Inspector::buildAxis(int gid, int a)
{
    const Graph *g = m_doc->project().graph(gid);
    if (!g) {
        buildPage();
        return;
    }
    static const char *an[] = {"Ось X", "Ось Y", "Верхняя ось X", "Правая ось Y"};
    header(T(an[a]), T("График G%1").arg(gid), "axis");
    // переключатель осей
    auto *sw = new QWidget(m_host);
    auto *sl = new QHBoxLayout(sw);
    sl->setContentsMargins(12, 0, 12, 6);
    sl->setSpacing(0);
    static const char *shortN[] = {"X", "Y", "X′", "Y′"};
    for (int k = 0; k < 4; ++k) {
        auto *b = new QToolButton(sw);
        b->setText(T(shortN[k]));
        b->setCheckable(true);
        b->setChecked(k == a);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        b->setObjectName("segment");
        connect(b, &QToolButton::clicked, this, [this, gid, k] { m_doc->select(Sel::axisOf(gid, k)); });
        sl->addWidget(b);
    }
    auto *back = new QToolButton(sw);
    back->setText(T("График…"));
    connect(back, &QToolButton::clicked, this, [this, gid] { m_doc->select(Sel::graph(gid)); });
    sl->addSpacing(8);
    sl->addWidget(back);
    m_lay->addWidget(sw);

    PropTarget t{PropTarget::AxisP, gid, -1, a};
    Section *m = section(T("Ось"));
    addFields(m, t, {fld("on", T("Ось включена")), fld("label", T("Подпись")), fld("label_font", T("Шрифт")),
                     num("label_char_size", T("Размер"), 0, 20, 0.05), fld("label_color", T("Цвет")),
                     choice("label_layout", T("Направление"), {"para", "perp"}, {T("Вдоль оси"), T("Поперёк")}),
                     choice("label_side", T("Сторона"), kPlaceV, kPlaceL),
                     choice("label_place_type", T("Размещение"), kAutoV, kAutoL),
                     pair("label_place_xy", T("Смещение подписи"), {T("вдоль"), T("поперёк")})});
    Section *ln = section(T("Линия оси"), false);
    addFields(ln, t, {fld("zero", T("Проходит через ноль")), pair("offset", T("Отступ от рамки"), {T("обычн."), T("напр.")}),
                      fld("bar", T("Рисовать линию оси")), fld("bar_color", T("Цвет")), fld("bar_linewidth", T("Толщина")),
                      fld("bar_linestyle", T("Стиль"))});
    Section *tk = section(T("Деления"));
    addFields(tk, t, {fld("tick", T("Показывать деления")),
                      tip(num("tick_major", T("Шаг основных"), -1e15, 1e15, 0.1, 8),
                          T("Для логарифмической шкалы — множитель (обычно 10)")),
                      ispin("tick_minor_ticks", T("Промежуточных"), 0, 100),
                      tip(ispin("tick_default", T("Число делений (авто)"), 2, 50), T("Используется при автоматическом подборе шага")),
                      fld("tick_rounded", T("Начинать с круглого значения")),
                      choice("tick_inout", T("Направление"), {"in", "out", "both"}, {T("Внутрь"), T("Наружу"), T("В обе стороны")}),
                      choice("tick_place", T("Сторона"), kPlaceV, kPlaceL)});
    Section *mj = section(T("Вид делений и сетка"), false);
    addFields(mj, t, {num("tick_major_size", T("Длина основных"), 0, 10, 0.1), fld("tick_major_color", T("Цвет основных")),
                      fld("tick_major_linewidth", T("Толщина основных")), fld("tick_major_linestyle", T("Стиль основных")),
                      fld("tick_major_grid", T("Сетка по основным")),
                      num("tick_minor_size", T("Длина промежуточных"), 0, 10, 0.1),
                      fld("tick_minor_color", T("Цвет промежуточных")), fld("tick_minor_linewidth", T("Толщина промежуточных")),
                      fld("tick_minor_linestyle", T("Стиль промежуточных")), fld("tick_minor_grid", T("Сетка по промежуточным"))});
    Section *tl = section(T("Подписи делений"));
    addFields(tl, t, {fld("ticklabel", T("Показывать подписи")), fld("ticklabel_format", T("Формат")),
                      ispin("ticklabel_prec", T("Знаков"), 0, 15), fld("ticklabel_font", T("Шрифт")),
                      num("ticklabel_char_size", T("Размер"), 0, 20, 0.05), fld("ticklabel_color", T("Цвет")),
                      ispin("ticklabel_angle", T("Угол, °"), 0, 360), ispin("ticklabel_skip", T("Пропускать"), 0, 100),
                      ispin("ticklabel_stagger", T("Ступенчато"), 0, 10),
                      choice("ticklabel_place", T("Сторона"), kPlaceV, kPlaceL), fld("ticklabel_prepend", T("Перед числом")),
                      fld("ticklabel_append", T("После числа")),
                      tip(fld("ticklabel_formula", T("Формула")), T("Пересчёт подписи: переменная $t, например $t - 273.15"))});
    Section *tla = section(T("Подписи: размещение"), false);
    addFields(tla, t, {choice("ticklabel_offset_type", T("Отступ"), kAutoV, kAutoL),
                       pair("ticklabel_offset", T("Отступ"), {T("вдоль"), T("поперёк")}),
                       choice("ticklabel_start_type", T("Начало"), kAutoV, kAutoL), num("ticklabel_start", T("С"), -1e15, 1e15, 1, 6),
                       choice("ticklabel_stop_type", T("Конец"), kAutoV, kAutoL), num("ticklabel_stop", T("По"), -1e15, 1e15, 1, 6)});
    Section *sp = section(T("Особые деления"), false);
    addFields(sp, t, {choice("tick_spec_type", T("Режим"), {"none", "ticks", "both"},
                             {T("Обычные"), T("Свои положения"), T("Свои положения и подписи")})});
    auto *eb = new QPushButton(T("Редактировать список…"), sp);
    connect(eb, &QPushButton::clicked, this, [this, gid, a] { emit editTicksWanted(gid, a); });
    sp->body()->addWidget(eb);
}

// ---- набор

void Inspector::buildSet(int gid, int sid)
{
    const Graph *g = m_doc->project().graph(gid);
    const DataSet *s = g ? g->set(sid) : nullptr;
    if (!s) {
        buildPage();
        return;
    }
    const QVector<Sel> multi = m_doc->selectedSets();
    const bool group = multi.size() > 1 && m_doc->isSetSelected(gid, sid);
    if (group) {
        QStringList names;
        for (const Sel &x : multi)
            names << QString("G%1.S%2").arg(x.gid).arg(x.sid);
        header(T("Выбрано наборов: %1").arg(multi.size()),
               T("Изменения применяются ко всем: %1").arg(names.join(", ")), "set");
    } else {
        header(T("Набор S%1").arg(sid), T("График G%1 · %2 · точек: %3").arg(gid).arg(s->type()).arg(s->size()), "set");
    }
    PropTarget t{PropTarget::SetP, gid, sid};

    // ---- быстрые действия: цвет, толщина, заливка до оси
    Section *q = section(T("Быстро"));
    {
        auto *cb = new ColorButton(m_doc, q);
        q->form()->addRow(T("Цвет набора"), cb);
        cb->setToolTip(T("Один цвет для линии, символов и погрешностей"));
        connect(cb, &ColorButton::picked, this, [this](int c) {
            const QVector<Sel> sets = m_doc->selectedSets();
            m_doc->modify(T("Цвет набора"), [&](Project &pr) {
                for (const Sel &x : sets)
                    if (Graph *gg = pr.graph(x.gid))
                        if (DataSet *d = gg->set(x.sid))
                            setSetColor(*d, c);
            });
        });
        m_refreshers << [this, cb, gid, sid] {
            const Graph *gg = m_doc->project().graph(gid);
            const DataSet *d = gg ? gg->set(sid) : nullptr;
            if (d)
                cb->setIndex(d->p.i("line_color"));
        };
        auto *lw = new GSpinBox(q);
        lw->setRange(0, 50);
        lw->setSingleStep(0.5);
        q->form()->addRow(T("Толщина линии"), lw);
        connect(lw, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v) {
            if (m_refreshing)
                return;
            m_doc->setPropMany(m_doc->selectedSetTargets().isEmpty() ? QVector<PropTarget>() : m_doc->selectedSetTargets(),
                               "line_linewidth", v, T("Толщина линии"));
        });
        m_refreshers << [this, lw, gid, sid] {
            const Graph *gg = m_doc->project().graph(gid);
            const DataSet *d = gg ? gg->set(sid) : nullptr;
            QSignalBlocker b(lw);
            if (d)
                lw->setValue(d->p.d("line_linewidth"));
        };
        auto *fill = new QComboBox(q);
        fill->addItem(T("Нет"), 0);
        fill->addItem(T("До оси X (y = 0)"), 1);
        fill->addItem(T("До нижнего края графика"), 2);
        fill->addItem(T("До верхнего края графика"), 3);
        fill->setMinimumWidth(40);
        q->form()->addRow(T("Заливка под кривой"), fill);
        fill->setToolTip(T("Закрасить область между кривой и осью X светлым оттенком цвета линии"));
        connect(fill, QOverload<int>::of(&QComboBox::activated), this, [this, fill](int i) {
            const int mode = fill->itemData(i).toInt();
            const QVector<Sel> sets = m_doc->selectedSets();
            m_doc->modify(T("Заливка под кривой"), [&](Project &pr) {
                for (const Sel &x : sets)
                    fillToAxis(pr, x.gid, x.sid, mode);
            });
        });
        m_refreshers << [this, fill, gid, sid] {
            const Graph *gg = m_doc->project().graph(gid);
            const DataSet *d = gg ? gg->set(sid) : nullptr;
            QSignalBlocker b(fill);
            const int mode = d ? fillToAxisMode(*d) : 0;
            if (mode < 0) {
                if (fill->findData(-1) < 0)
                    fill->addItem(T("Своя (см. «Заливка»)"), -1);
                fill->setCurrentIndex(fill->findData(-1));
            } else {
                fill->setCurrentIndex(fill->findData(mode));
            }
        };
        if (group) {
            auto *dc = new QPushButton(icon("palette"), T("Разные цвета по порядку"), q);
            connect(dc, &QPushButton::clicked, this, [this] {
                QVector<QPair<int, int>> v;
                for (const Sel &x : m_doc->selectedSets())
                    v << qMakePair(x.gid, x.sid);
                m_doc->modify(T("Разные цвета"), [&](Project &pr) { distinctColors(pr, v); });
            });
            q->body()->addWidget(dc);
        }
    }

    Section *m = section(T("Набор"));
    if (group)
        addFields(m, t, {fld("type", T("Тип данных")), fld("hidden", T("Скрыт"))});
    else
        addFields(m, t, {fld("legend", T("Легенда")), tip(fld("comment", T("Комментарий")), T("Не отображается на графике")),
                         fld("type", T("Тип данных")), fld("hidden", T("Скрыт"))});
    const QString type = s->type();
    const bool bar = type.startsWith("bar") || type == "xyboxplot" || type == "xyhilo";
    Section *sy = section(bar ? T("Столбцы") : T("Символы"));
    QList<Field> symf;
    if (!bar)
        symf << fld("symbol", T("Символ"));
    symf << num("symbol_size", bar ? T("Ширина") : T("Размер"), 0, 20, 0.05) << fld("symbol_color", T("Цвет контура"))
         << fld("symbol_pattern", T("Контур")) << fld("symbol_fill_color", T("Цвет заливки"))
         << fld("symbol_fill_pattern", T("Заливка")) << fld("symbol_linewidth", T("Толщина контура"))
         << fld("symbol_linestyle", T("Стиль контура"));
    if (!bar)
        symf << ispin("symbol_char", T("Код символа"), 0, 65535) << fld("symbol_char_font", T("Шрифт символа"));
    symf << ispin("symbol_skip", T("Пропускать точек"), 0, 100000);
    addFields(sy, t, symf);
    Section *ln = section(T("Линия"));
    addFields(ln, t, {choice("line_type", T("Соединение"), ints(6),
                             {T("Нет"), T("Прямые"), T("Ступеньки ⌐"), T("Ступеньки ¬"), T("Отрезки по 2 точки"), T("Отрезки по 3 точки")}),
                      fld("line_linestyle", T("Стиль")), fld("line_linewidth", T("Толщина")), fld("line_color", T("Цвет")),
                      fld("line_pattern", T("Узор")), fld("dropline", T("Выносные линии")), fld("baseline", T("Базовая линия")),
                      choice("baseline_type", T("Уровень базы"), ints(6),
                             {"y = 0", T("Минимум набора"), T("Максимум набора"), T("Низ графика"), T("Верх графика"), T("Среднее набора")})});
    Section *fl = section(T("Заливка"), false);
    addFields(fl, t, {choice("fill_type", T("Заливка"), ints(3), {T("Нет"), T("Многоугольник"), T("До базовой линии")}),
                      choice("fill_rule", T("Правило"), ints(2), {T("Ненулевое"), T("Чётно-нечётное")}),
                      fld("fill_color", T("Цвет")), fld("fill_pattern", T("Узор"))});
    const bool err = type.startsWith("xyd") || type == "bardy" || type == "bardydy" || type == "xyboxplot" || type == "xyvmap";
    Section *eb = section(T("Погрешности"), err);
    addFields(eb, t, {fld("errorbar", T("Показывать")),
                      choice("errorbar_place", T("Направление"), kPlaceV, {T("Только +"), T("Только −"), "±"}),
                      fld("errorbar_color", T("Цвет")), fld("errorbar_pattern", T("Узор")),
                      num("errorbar_size", T("Ширина засечки"), 0, 20, 0.1), fld("errorbar_linewidth", T("Толщина засечки")),
                      fld("errorbar_linestyle", T("Стиль засечки")), fld("errorbar_riser_linewidth", T("Толщина стойки")),
                      fld("errorbar_riser_linestyle", T("Стиль стойки")), fld("errorbar_riser_clip", T("Ограничить длину стойки")),
                      num("errorbar_riser_clip_length", T("Макс. длина"), 0, 10, 0.01, 3)});
    Section *av = section(T("Подписи значений"), false);
    addFields(av, t, {fld("avalue", T("Показывать")),
                      choice("avalue_type", T("Что писать"), ints(6), {T("Ничего"), "X", "Y", "X, Y", T("Строка"), "Z"}),
                      fld("avalue_format", T("Формат")), ispin("avalue_prec", T("Знаков"), 0, 15), fld("avalue_font", T("Шрифт")),
                      num("avalue_char_size", T("Размер"), 0, 20, 0.05), fld("avalue_color", T("Цвет")),
                      ispin("avalue_rot", T("Угол, °"), 0, 360), fld("avalue_prepend", T("Перед")),
                      fld("avalue_append", T("После")), pair("avalue_offset", T("Смещение"), {"x", "y"})});
    Section *stt = section(T("Статистика"), false);
    auto *lbl = new QLabel(stt);
    lbl->setTextInteractionFlags(Qt::TextSelectableByMouse);
    lbl->setWordWrap(true);
    lbl->setObjectName("mono");
    stt->body()->addWidget(lbl);
    m_refreshers << [this, lbl, gid, sid] {
        const Graph *gg = m_doc->project().graph(gid);
        const DataSet *ss = gg ? gg->set(sid) : nullptr;
        lbl->setText(ss ? statistics(*ss) : QString());
    };
}

// ---- объект

void Inspector::buildObject(int idx)
{
    const Project &pr = m_doc->project();
    if (idx < 0 || idx >= pr.objects.size()) {
        buildPage();
        return;
    }
    const DrawObject &o = pr.objects[idx];
    static const QHash<QString, QString> ic = {{"string", "text"}, {"line", "line"}, {"box", "box"}, {"ellipse", "ellipse"}};
    header(o.displayName().section(" · ", 0, 0), T("Перетаскивайте мышью; стрелки — точный сдвиг"),
           ic.value(o.kind, "shapes"));
    PropTarget t{PropTarget::ObjectP, -1, -1, 0, idx};
    Section *m = section(T("Свойства"));
    QList<Field> f;
    f << fld("on", T("Показывать"))
      << choice("loctype", T("Координаты"), {"view", "world"}, {T("Страница"), T("Как у графика")});
    if (o.kind == "string") {
        f << fld("text", T("Текст")) << pair("pos", T("Положение"), {"x", "y"}, 6) << fld("font", T("Шрифт"))
          << num("char_size", T("Размер"), 0, 50, 0.1) << fld("color", T("Цвет")) << ispin("rot", T("Угол, °"), 0, 360)
          << choice("just", T("Выравнивание"), {0, 2, 1, 4, 6, 5, 12, 14, 13, 8, 10, 9},
                    {T("Слева, по базе"), T("По центру, по базе"), T("Справа, по базе"), T("Слева снизу"), T("По центру снизу"),
                     T("Справа снизу"), T("Слева посередине"), T("В центре"), T("Справа посередине"), T("Слева сверху"),
                     T("По центру сверху"), T("Справа сверху")});
    } else if (o.kind == "line") {
        f << quad("coords", T("Концы"), {"x₁", "y₁", "x₂", "y₂"}) << fld("color", T("Цвет")) << fld("linewidth", T("Толщина"))
          << fld("linestyle", T("Стиль"))
          << choice("arrow", T("Стрелка"), ints(4), {T("Нет"), T("В начале"), T("В конце"), T("С обеих сторон")})
          << choice("arrow_type", T("Вид стрелки"), ints(3), {T("Линии"), T("Закрашенная"), T("Непрозрачная")})
          << num("arrow_length", T("Длина стрелки"), 0, 20, 0.1)
          << pair("arrow_layout", T("Форма (ширина, вырез)"), {"d/L", "l/L"}, 2);
    } else {
        f << quad("coords", T("Углы"), {"x₁", "y₁", "x₂", "y₂"}) << fld("color", T("Цвет линии")) << fld("linewidth", T("Толщина"))
          << fld("linestyle", T("Стиль")) << fld("fill_color", T("Цвет заливки")) << fld("fill_pattern", T("Заливка"));
    }
    addFields(m, t, f);
    if (o.p.s("loctype") == "world") {
        auto *sb = new QSpinBox(m);
        sb->setRange(0, 999);
        sb->setPrefix("G");
        m->form()->addRow(T("График"), sb);
        connect(sb, QOverload<int>::of(&QSpinBox::valueChanged), this, [this, idx](int v) {
            if (m_refreshing) return;
            m_doc->modify(T("График объекта"), [&](Project &p) {
                if (idx < p.objects.size()) p.objects[idx].graph = v;
            });
        });
        m_refreshers << [this, sb, idx] {
            QSignalBlocker b(sb);
            if (idx < m_doc->project().objects.size())
                sb->setValue(m_doc->project().objects[idx].graph);
        };
    }
}

}  // namespace ng
