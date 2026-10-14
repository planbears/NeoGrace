#include "dialogs.h"
#include "i18n.h"

#include "axes.h"
#include "gracetext.h"
#include "icons.h"
#include "model.h"
#include "expr.h"
#include "export.h"
#include "ops.h"
#include "setmath.h"

#include <algorithm>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImage>
#include <QPixmap>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QRadioButton>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <cmath>

namespace ng {

static QLabel *hintLabel(const QString &t, QWidget *parent)
{
    auto *l = new QLabel(t, parent);
    l->setWordWrap(true);
    l->setObjectName("subtle");
    return l;
}

// Число или выражение («2*pi», «1e-3», «0,5»)
static bool numberExpr(const QString &text, double &v)
{
    QString t = text.trimmed();
    if (t.isEmpty())
        return false;
    bool ok = false;
    v = QLocale::c().toDouble(QString(t).replace(',', '.'), &ok);
    if (ok)
        return std::isfinite(v);
    QVector<double> out;
    if (!evalOnArray(t, {0.0}, out) || out.isEmpty())
        return false;
    v = out[0];
    return std::isfinite(v);
}

// =============================================================================
// Импорт

ImportDialog::ImportDialog(Document *doc, const QString &path, QWidget *parent)
    : QDialog(parent), m_doc(doc), m_path(path)
{
    setWindowTitle(T("Импорт данных — %1").arg(QFileInfo(path).fileName()));
    resize(680, 560);
    QFile f(path);
    if (f.open(QIODevice::ReadOnly))
        m_text = decodeBytes(f.read(50 * 1024 * 1024));
    auto *lay = new QVBoxLayout(this);
    m_preview = new QPlainTextEdit(this);
    m_preview->setReadOnly(true);
    m_preview->setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont mono("Menlo");
    mono.setStyleHint(QFont::Monospace);
    mono.setPointSizeF(font().pointSizeF() * 0.9);
    m_preview->setFont(mono);
    m_preview->setPlainText(m_text.left(6000));
    m_preview->setMinimumHeight(140);
    lay->addWidget(m_preview, 1);
    m_info = new QLabel(this);
    lay->addWidget(m_info);

    auto *form = new QFormLayout();
    m_form = form;
    m_graph = new QComboBox(this);
    for (const Graph &g : doc->project().graphs)
        m_graph->addItem(g.displayName(), g.id);
    m_graph->addItem(T("Новый график"), -1);
    const int cg = doc->currentGraph();
    m_graph->setCurrentIndex(qMax(0, m_graph->findData(cg)));
    form->addRow(T("Куда"), m_graph);
    m_blank = new QCheckBox(T("Пустая строка разделяет блоки"), this);
    form->addRow(QString(), m_blank);
    m_block = new QComboBox(this);
    form->addRow(T("Блок"), m_block);
    m_mode = new QComboBox(this);
    m_mode->addItem(T("Первый столбец — X, остальные — отдельные наборы Y"), "nxy");
    m_mode->addItem(T("Один набор из выбранных столбцов"), "single");
    form->addRow(T("Как читать"), m_mode);
    m_type = new QComboBox(this);
    for (const QString &t : kSetTypes)
        m_type->addItem(t + "  (" + setColumnNames(t).join(", ") + ")", t);
    form->addRow(T("Тип набора"), m_type);
    lay->addLayout(form);
    m_colsBox = new QWidget(this);
    new QGridLayout(m_colsBox);
    lay->addWidget(m_colsBox);
    m_autoscale = new QCheckBox(T("Подобрать масштаб"), this);
    m_autoscale->setChecked(true);
    lay->addWidget(m_autoscale);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    bb->button(QDialogButtonBox::Ok)->setText(T("Импортировать"));
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(bb);

    connect(m_blank, &QCheckBox::toggled, this, &ImportDialog::updatePreview);
    connect(m_mode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ImportDialog::updatePreview);
    connect(m_type, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ImportDialog::updatePreview);
    connect(m_block, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ImportDialog::updatePreview);
    updatePreview();
}

void ImportDialog::updatePreview()
{
    m_tables = readTables(m_text, m_blank->isChecked());
    {
        QSignalBlocker b(m_block);
        const int cur = m_block->currentIndex();
        m_block->clear();
        m_block->addItem(T("Все блоки"), -1);
        for (int i = 0; i < m_tables.size(); ++i)
            m_block->addItem(T("Блок %1: %2 строк × %3 столбцов").arg(i + 1).arg(m_tables[i].rows()).arg(m_tables[i].cols.size()), i);
        m_block->setCurrentIndex(qBound(0, cur, m_block->count() - 1));
    }
    m_form->setRowVisible(m_block, m_tables.size() > 1);
    if (m_tables.isEmpty()) {
        m_info->setText(T("⚠︎ Числовых данных не найдено"));
        return;
    }
    int rows = 0, maxc = 0;
    for (const auto &t : m_tables) {
        rows += t.rows();
        maxc = qMax(maxc, int(t.cols.size()));
    }
    m_info->setText(T("Найдено блоков: %1, строк: %2, столбцов: до %3").arg(m_tables.size()).arg(rows).arg(maxc));
    const bool single = m_mode->currentData().toString() == "single";
    m_type->setEnabled(single);
    // выбор столбцов
    auto *g = static_cast<QGridLayout *>(m_colsBox->layout());
    while (auto *it = g->takeAt(0)) {
        delete it->widget();
        delete it;
    }
    m_colBoxes.clear();
    m_colsBox->setVisible(single);
    if (!single)
        return;
    const QString type = m_type->currentData().toString();
    const QStringList names = setColumnNames(type);
    const Table &t0 = m_tables.first();
    for (int k = 0; k < names.size(); ++k) {
        auto *cb = new QComboBox(m_colsBox);
        for (int c = 0; c < maxc; ++c)
            cb->addItem(T("столбец %1%2").arg(c + 1).arg(c < t0.headers.size() ? " · " + t0.headers[c] : QString()), c);
        cb->addItem(T("нули"), -1);
        cb->setCurrentIndex(qMin(k, maxc));
        g->addWidget(new QLabel(names[k], m_colsBox), k / 3, (k % 3) * 2);
        g->addWidget(cb, k / 3, (k % 3) * 2 + 1);
        m_colBoxes << cb;
    }
}

int ImportDialog::apply()
{
    int gid = m_graph->currentData().toInt();
    const QString mode = m_mode->currentData().toString();
    const QString type = m_type->currentData().toString();
    QVector<int> cols;
    for (auto *cb : m_colBoxes)
        cols << cb->currentData().toInt();
    const int block = m_block->currentData().toInt();
    const bool autoscale = m_autoscale->isChecked();
    const QString base = QFileInfo(m_path).completeBaseName();
    m_doc->modify(T("Импорт %1").arg(QFileInfo(m_path).fileName()), [&](Project &pr) {
        if (gid < 0 || !pr.graph(gid)) {
            const bool empty = pr.graphs.size() == 1 && pr.graphs[0].sets.isEmpty();
            if (empty) {
                gid = pr.graphs[0].id;
            } else {
                Graph ng(pr.nextGraphId());
                if (!pr.graphs.isEmpty()) {
                    ng = pr.graphs.first();
                    ng.id = pr.nextGraphId();
                    ng.sets.clear();
                    ng.p.set("title", QString());
                    ng.p.set("subtitle", QString());
                }
                pr.graphs.append(ng);
                gid = ng.id;
            }
        }
        Graph &g = *pr.graph(gid);
        const int before = g.sets.size();
        for (int i = 0; i < m_tables.size(); ++i) {
            if (block >= 0 && i != block)
                continue;
            addTableAsSets(g, m_tables[i], mode, type, cols);
        }
        for (int k = before; k < g.sets.size(); ++k) {
            DataSet &s = g.sets[k];
            if (s.p.s("comment").isEmpty())
                s.p.set("comment", base);
            if (s.p.s("legend").isEmpty() && g.sets.size() - before > 1)
                s.p.set("legend", QStringLiteral("%1 · %2").arg(base).arg(k - before + 1));
            if (s.size() < 30 && s.p.i("symbol") == 0) {
                s.p.set("symbol", 1);
                s.p.set("symbol_size", 0.6);
                s.p.set("symbol_fill_pattern", 1);
            }
            if (s.type() != "xy" && s.type() != "bar")
                s.p.set("line_type", s.type().startsWith("xyd") ? 0 : s.p.i("line_type"));
            if (s.type().startsWith("xyd") && s.p.i("symbol") == 0) {
                s.p.set("symbol", 1);
                s.p.set("symbol_size", 0.6);
                s.p.set("symbol_fill_pattern", 1);
            }
        }
        if (autoscale)
            autoscaleGraph(g);
    });
    return gid;
}

// =============================================================================
// Анализ

AnalysisDialog::AnalysisDialog(Document *doc, int tab, QWidget *parent) : QDialog(parent), m_doc(doc)
{
    setWindowTitle(T("Анализ данных"));
    resize(560, 520);
    auto *lay = new QVBoxLayout(this);
    auto *top = new QFormLayout();
    m_set = new QComboBox(this);
    top->addRow(T("Набор"), m_set);
    lay->addLayout(top);
    m_tabs = new QTabWidget(this);
    lay->addWidget(m_tabs, 1);

    // --- формула
    {
        auto *w = new QWidget;
        auto *l = new QVBoxLayout(w);
        l->addWidget(hintLabel(T("Присваивания через «;» или с новой строки. Переменные: x, y, y1…y4, " "i (номер точки), n; столбцы других наборов: s1.y, s2.x…"), w));
        m_formula = new QPlainTextEdit(w);
        m_formula->setPlaceholderText("y = y * 2 + 1\nx = x / 1000");
        l->addWidget(m_formula);
        auto *ex = new QHBoxLayout();
        for (const char *e : {"y = abs(y)", "y = y - min(y, 0)", "x = x / 60", "y = log10(y)", "y = y / s1.y"}) {
            auto *b = new QToolButton(w);
            b->setText(T(e));
            connect(b, &QToolButton::clicked, this, [this, e] { m_formula->setPlainText(T(e)); });
            ex->addWidget(b);
        }
        ex->addStretch(1);
        l->addLayout(ex);
        m_tabs->addTab(w, T("Формула"));
    }
    // --- новый набор
    {
        auto *w = new QWidget;
        auto *f = new QFormLayout(w);
        m_newY = new QLineEdit("sin(x) * exp(-x/5)", w);
        m_newX = new QLineEdit(w);
        m_newX->setPlaceholderText(T("x (по умолчанию); например, 10^t для лог. сетки"));
        m_from = new QDoubleSpinBox(w);
        m_to = new QDoubleSpinBox(w);
        for (auto *sb : {m_from, m_to}) {
            sb->setRange(-1e12, 1e12);
            sb->setDecimals(6);
        }
        m_from->setValue(0);
        m_to->setValue(10);
        m_npts = new QSpinBox(w);
        m_npts->setRange(2, 10000000);
        m_npts->setValue(200);
        f->addRow("y =", m_newY);
        f->addRow(T("От"), m_from);
        f->addRow(T("До"), m_to);
        f->addRow(T("Точек"), m_npts);
        f->addRow("x =", m_newX);
        f->addRow(hintLabel(T("Набор создаётся в текущем графике. Функции: sin cos tan exp ln log10 sqrt abs " "erf gamma j0 j1 rand rnorm(μ,σ) min max…"), w));
        m_tabs->addTab(w, T("Новый набор"));
    }
    // --- регрессия
    {
        auto *w = new QWidget;
        auto *f = new QFormLayout(w);
        m_regKind = new QComboBox(w);
        m_regKind->addItem(T("Линейная  y = a + b·x"), "linear");
        m_regKind->addItem(T("Полином  y = a₀ + a₁x + … + aₙxⁿ"), "poly");
        m_regKind->addItem(T("Экспонента  y = a·exp(b·x)"), "exp");
        m_regKind->addItem(T("Степенная  y = a·xᵇ"), "power");
        m_regKind->addItem(T("Логарифм  y = a + b·ln x"), "log");
        m_degree = new QSpinBox(w);
        m_degree->setRange(1, 10);
        m_degree->setValue(2);
        f->addRow(T("Модель"), m_regKind);
        f->addRow(T("Степень полинома"), m_degree);
        m_tabs->addTab(w, T("Регрессия"));
    }
    // --- нелинейная аппроксимация
    {
        auto *w = new QWidget;
        auto *l = new QVBoxLayout(w);
        auto *f = new QFormLayout();
        m_fitExpr = new QLineEdit("a0 * exp(-x / a1) + a2", w);
        f->addRow("y =", m_fitExpr);
        auto *presets = new QComboBox(w);
        presets->addItem(T("Шаблоны…"));
        const QStringList pv = {"a0 * exp(-x / a1) + a2", "a0 * exp(-(x - a1)^2 / (2 * a2^2)) + a3",
                                "a0 / (1 + ((x - a1) / a2)^2) + a3", "a0 / (1 + exp(-(x - a1) / a2))",
                                "a0 * x^a1", "a0 * sin(a1 * x + a2) + a3", "a0 * (1 - exp(-x / a1))"};
        const QStringList pn = {T("Экспоненциальный спад"), T("Гаусс"), T("Лоренц"), T("Логистическая кривая"), T("Степенная"),
                                T("Синусоида"), T("Насыщение")};
        for (int i = 0; i < pv.size(); ++i)
            presets->addItem(pn[i], pv[i]);
        connect(presets, QOverload<int>::of(&QComboBox::activated), this, [this, presets](int i) {
            if (i > 0)
                m_fitExpr->setText(presets->itemData(i).toString());
        });
        f->addRow(QString(), presets);
        m_iter = new QSpinBox(w);
        m_iter->setRange(1, 100000);
        m_iter->setValue(500);
        f->addRow(T("Итераций не больше"), m_iter);
        auto *rng = new QHBoxLayout();
        m_fitRange = new QCheckBox(T("только x от"), w);
        m_fitX1 = new QDoubleSpinBox(w);
        m_fitX2 = new QDoubleSpinBox(w);
        for (auto *sb : {m_fitX1, m_fitX2}) {
            sb->setRange(-1e15, 1e15);
            sb->setDecimals(6);
        }
        rng->addWidget(m_fitRange);
        rng->addWidget(m_fitX1);
        rng->addWidget(new QLabel(T("до"), w));
        rng->addWidget(m_fitX2);
        f->addRow(T("Диапазон"), rng);
        l->addLayout(f);
        m_params = new QTableWidget(10, 2, w);
        m_params->setHorizontalHeaderLabels({T("Начальное"), T("Результат")});
        for (int i = 0; i < 10; ++i) {
            m_params->setVerticalHeaderItem(i, new QTableWidgetItem(QString("a%1").arg(i)));
            m_params->setItem(i, 0, new QTableWidgetItem("1"));
            auto *r = new QTableWidgetItem("");
            r->setFlags(r->flags() & ~Qt::ItemIsEditable);
            m_params->setItem(i, 1, r);
        }
        m_params->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        m_params->verticalHeader()->setDefaultSectionSize(20);
        l->addWidget(m_params, 1);
        m_tabs->addTab(w, T("Аппроксимация"));
    }
    // --- преобразования
    {
        auto *w = new QWidget;
        auto *f = new QFormLayout(w);
        m_op = new QComboBox(w);
        m_op->addItem(T("Скользящее среднее"), "smooth");
        m_op->addItem(T("Производная"), "deriv");
        m_op->addItem(T("Интеграл (накопленный)"), "integ");
        m_op->addItem(T("Спектр Фурье"), "fft");
        m_op->addItem(T("Гистограмма значений Y"), "hist");
        m_op->addItem(T("Интерполяция на равномерную сетку"), "interp");
        m_op->addItem(T("Сортировка"), "sort");
        m_op->addItem(T("Копия набора"), "dup");
        f->addRow(T("Операция"), m_op);
        m_window = new QSpinBox(w);
        m_window->setRange(1, 100000);
        m_window->setValue(5);
        f->addRow(T("Окно сглаживания"), m_window);
        m_power = new QCheckBox(T("Мощность вместо амплитуды"), w);
        f->addRow(T("Спектр"), m_power);
        m_bins = new QSpinBox(w);
        m_bins->setRange(1, 100000);
        m_bins->setValue(20);
        m_normalize = new QCheckBox(T("Нормировать (плотность)"), w);
        f->addRow(T("Интервалов"), m_bins);
        f->addRow(QString(), m_normalize);
        m_interpN = new QSpinBox(w);
        m_interpN->setRange(2, 10000000);
        m_interpN->setValue(500);
        m_spline = new QCheckBox(T("Кубический сплайн"), w);
        m_spline->setChecked(true);
        f->addRow(T("Точек"), m_interpN);
        f->addRow(QString(), m_spline);
        m_byY = new QCheckBox(T("По Y (иначе по X)"), w);
        m_desc = new QCheckBox(T("По убыванию"), w);
        f->addRow(T("Сортировка"), m_byY);
        f->addRow(QString(), m_desc);
        m_tabs->addTab(w, T("Преобразования"));
    }
    m_result = new QLabel(this);
    m_result->setWordWrap(true);
    m_result->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_result->setObjectName("result");
    m_result->setMinimumHeight(60);
    m_result->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    lay->addWidget(m_result);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto *runB = bb->addButton(T("Выполнить"), QDialogButtonBox::ApplyRole);
    runB->setDefault(true);
    connect(runB, &QPushButton::clicked, this, &AnalysisDialog::run);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(bb);
    m_tabs->setCurrentIndex(tab);
    fillSets();
    connect(doc, &Document::changed, this, &AnalysisDialog::fillSets);
}

void AnalysisDialog::fillSets()
{
    const QVariant cur = m_set->currentData();
    QSignalBlocker b(m_set);
    m_set->clear();
    for (const Graph &g : m_doc->project().graphs)
        for (const DataSet &s : g.sets)
            m_set->addItem(QString("G%1.%2").arg(g.id).arg(s.displayName()), QPoint(g.id, s.id));
    int idx = cur.isValid() ? m_set->findData(cur) : -1;
    if (idx < 0) {
        const Sel &s = m_doc->selection();
        if (s.kind == Sel::Set)
            idx = m_set->findData(QPoint(s.gid, s.sid));
    }
    m_set->setCurrentIndex(qMax(0, idx));
}

void AnalysisDialog::run()
{
    const QPoint gs = m_set->currentData().toPoint();
    const int gid = m_set->count() ? gs.x() : m_doc->currentGraph();
    const int sid = gs.y();
    const int tab = m_tabs->currentIndex();
    QString msg;
    bool ok = true;
    int newSet = -1;
    if (tab == 1) {
        QString err;
        const QString y = m_newY->text(), x = m_newX->text();
        const double a = m_from->value(), b = m_to->value();
        const int n = m_npts->value();
        const int g0 = m_doc->currentGraph();
        // проверим формулу заранее
        Project test = m_doc->project();
        if (test.graphs.isEmpty())
            test.graphOrCreate(0);
        Graph &tg = test.graph(g0) ? *test.graph(g0) : test.graphs[0];
        if (createFromFormula(tg, a, b, n, y, &err, x) < 0) {
            m_result->setText("⚠︎ " + err);
            return;
        }
        m_doc->modify(T("Набор по формуле"), [&](Project &pr) {
            if (pr.graphs.isEmpty())
                pr.graphOrCreate(0);
            Graph &g = pr.graph(g0) ? *pr.graph(g0) : pr.graphs[0];
            const bool first = g.sets.isEmpty();
            newSet = createFromFormula(g, a, b, n, y, &err, x);
            if (first)
                autoscaleGraph(g);
        });
        m_result->setText(T("Создан набор S%1").arg(newSet));
        return;
    }
    const Graph *g0 = m_doc->project().graph(gid);
    if (!g0 || !g0->set(sid)) {
        m_result->setText(T("⚠︎ Выберите набор"));
        return;
    }
    if (tab == 0) {
        QString err;
        Project test = m_doc->project();
        Graph &tg = *test.graph(gid);
        if (!applyFormula(tg, *tg.set(sid), m_formula->toPlainText(), &err)) {
            m_result->setText("⚠︎ " + err);
            return;
        }
        const QString text = m_formula->toPlainText();
        m_doc->modify(T("Формула"), [&](Project &pr) {
            Graph &g = *pr.graph(gid);
            applyFormula(g, *g.set(sid), text, &err);
        });
        m_result->setText(T("Готово"));
        return;
    }
    FitResult fr;
    m_doc->modify(m_tabs->tabText(tab), [&](Project &pr) {
        Graph &g = *pr.graph(gid);
        if (tab == 2) {
            fr = regression(g, sid, m_regKind->currentData().toString(), m_degree->value());
            ok = fr.ok;
            msg = fr.message;
            newSet = fr.newSet;
        } else if (tab == 3) {
            QVector<double> p0;
            for (int i = 0; i < 10; ++i)
                p0 << m_params->item(i, 0)->text().replace(',', '.').toDouble();
            const bool rng = m_fitRange->isChecked();
            fr = nonlinearFit(g, sid, m_fitExpr->text(), p0, m_iter->value(), 300, rng ? m_fitX1->value() : 0,
                              rng ? m_fitX2->value() : 0);
            ok = fr.ok;
            msg = fr.message;
            newSet = fr.newSet;
        } else {
            const QString op = m_op->currentData().toString();
            if (op == "smooth") newSet = runningAverage(g, sid, m_window->value());
            else if (op == "deriv") newSet = derivative(g, sid);
            else if (op == "integ") {
                double total = 0;
                newSet = integral(g, sid, &total);
                msg = T("Интеграл по всему набору: %1").arg(QString::number(total, 'g', 10));
            } else if (op == "fft") newSet = fourier(g, sid, m_power->isChecked());
            else if (op == "hist") newSet = histogram(g, sid, m_bins->value(), 0, 0, m_normalize->isChecked());
            else if (op == "interp") newSet = interpolate(g, sid, m_interpN->value(), m_spline->isChecked());
            else if (op == "sort") {
                sortSet(*g.set(sid), m_byY->isChecked(), m_desc->isChecked());
                msg = T("Набор упорядочен");
            } else if (op == "dup") newSet = duplicateSet(g, sid);
            if (op == "fft" || op == "hist") {
                // спектр и гистограмму удобнее смотреть в новом графике
                Graph ng = g;
                ng.id = pr.nextGraphId();
                ng.sets.clear();
                for (const DataSet &s : g.sets)
                    if (s.id == newSet)
                        ng.sets << s;
                g.sets.erase(std::remove_if(g.sets.begin(), g.sets.end(), [&](const DataSet &s) { return s.id == newSet; }),
                             g.sets.end());
                ng.sets[0].id = 0;
                ng.sets[0].p.set("legend", QString());
                ng.p.set("title", m_op->currentText());
                ng.p.set("subtitle", T("из G%1.S%2").arg(gid).arg(sid));
                ng.p.set("xscale", "Normal");
                ng.p.set("yscale", "Normal");
                autoscaleGraph(ng);
                pr.graphs.append(ng);
                msg = T("Результат — в новом графике G%1 (расставьте графики: Вид → Расставить)").arg(ng.id);
                newSet = -2;
            }
            if (newSet == -1 && op != "sort") {
                ok = false;
                msg = T("Недостаточно точек для операции");
            }
        }
    });
    if (!ok) {
        m_result->setText("⚠︎ " + msg);
        m_doc->undo()->undo();
        return;
    }
    if (tab == 3) {
        for (int i = 0; i < 10; ++i)
            m_params->item(i, 1)->setText(i < fr.params.size() && QRegularExpression(QString("\\ba%1\\b").arg(i)).match(m_fitExpr->text()).hasMatch()
                                              ? QString::number(fr.params[i], 'g', 8)
                                              : QString());
    }
    if (newSet >= 0 && msg.isEmpty())
        msg = T("Создан набор S%1").arg(newSet);
    else if (newSet >= 0)
        msg += T("\nРезультат — набор S%1").arg(newSet);
    m_result->setText(msg);
}

// =============================================================================
// Набор из текста

TextDataDialog::TextDataDialog(Document *doc, const QString &text, const Sel &replace, QWidget *parent)
    : QDialog(parent), m_doc(doc), m_replace(replace)
{
    setWindowTitle(T("Набор из текста"));
    resize(520, 520);
    auto *lay = new QVBoxLayout(this);
    lay->addWidget(hintLabel(T("Каждая строка — точка: X и Y через пробел, табуляцию или «;» (дробная часть — через точку). "
                               "Два числа дают точку на графике, две строки — отрезок. Пустая строка или «&» начинают новый набор. "
                               "Третий и следующие столбцы — погрешности или другие Y (см. «Тип»). "
                               "Строка вида «y = 2*x + 1» или «y = sin(x)» добавляет функцию."), this));
    m_text = new QPlainTextEdit(this);
    QFont mono("Menlo");
    mono.setStyleHint(QFont::Monospace);
    m_text->setFont(mono);
    m_text->setPlaceholderText("20.0 -10.0\n20.0 10.0\n\ny = 0.5*x");
    m_text->setPlainText(text);
    lay->addWidget(m_text, 1);
    m_info = new QLabel(this);
    m_info->setWordWrap(true);
    lay->addWidget(m_info);
    auto *form = new QFormLayout();
    m_target = new QComboBox(this);
    m_target->addItem(T("Новый набор"), 0);
    const Graph *rg = doc->project().graph(replace.gid);
    if (replace.kind == Sel::Set && rg && rg->set(replace.sid))
        m_target->addItem(T("Заменить данные набора G%1.S%2").arg(replace.gid).arg(replace.sid), 1);
    m_target->setCurrentIndex(m_target->count() - 1);
    form->addRow(T("Что сделать"), m_target);
    m_graph = new QComboBox(this);
    for (const Graph &g : doc->project().graphs)
        m_graph->addItem(g.displayName(), g.id);
    m_graph->addItem(T("Новый график"), -1);
    m_graph->setCurrentIndex(qMax(0, m_graph->findData(doc->currentGraph())));
    form->addRow(T("График"), m_graph);
    m_type = new QComboBox(this);
    m_type->addItem(T("Авто: X Y (больше столбцов — несколько Y)"), "auto");
    for (const QString &t : kSetTypes)
        m_type->addItem(t + "  (" + setColumnNames(t).join(", ") + ")", t);
    form->addRow(T("Тип"), m_type);
    m_legend = new QLineEdit(this);
    m_legend->setPlaceholderText(T("необязательно"));
    form->addRow(T("Легенда"), m_legend);
    // отрезок X для функций — по умолчанию видимая часть графика
    m_range = new QWidget(this);
    {
        auto *h = new QHBoxLayout(m_range);
        h->setContentsMargins(0, 0, 0, 0);
        double x1 = 0, x2 = 10;
        if (const Graph *g = doc->project().graph(doc->currentGraph())) {
            const Vec4 w = g->p.v4("world");
            x1 = qMin(w[0], w[2]);
            x2 = qMax(w[0], w[2]);
        }
        m_from = new QLineEdit(QString::number(x1, 'g', 10), m_range);
        m_to = new QLineEdit(QString::number(x2, 'g', 10), m_range);
        m_npts = new QSpinBox(m_range);
        m_npts->setRange(2, 10000000);
        m_npts->setValue(200);
        h->addWidget(m_from, 1);
        h->addWidget(new QLabel("…", m_range));
        h->addWidget(m_to, 1);
        h->addWidget(new QLabel(T("точек"), m_range));
        h->addWidget(m_npts);
    }
    form->addRow(T("X для функций"), m_range);
    m_fit = new QCheckBox(T("Расширить масштаб, если точки не помещаются"), this);
    m_fit->setChecked(true);
    form->addRow(QString(), m_fit);
    m_form = form;
    lay->addLayout(form);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_ok = bb->button(QDialogButtonBox::Ok);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(bb);
    connect(m_text, &QPlainTextEdit::textChanged, this, &TextDataDialog::update);
    connect(m_target, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &TextDataDialog::update);
    connect(m_type, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &TextDataDialog::update);
    connect(m_from, &QLineEdit::textChanged, this, &TextDataDialog::update);
    connect(m_to, &QLineEdit::textChanged, this, &TextDataDialog::update);
    update();
    m_text->setFocus();
}

void TextDataDialog::update()
{
    const bool replace = m_target->currentData().toInt() == 1;
    m_graph->setEnabled(!replace);
    m_legend->setEnabled(!replace);
    m_ok->setText(replace ? T("Заменить") : T("Добавить"));
    TextData d = parseDataText(m_text->toPlainText());
    m_form->setRowVisible(m_range, !d.formulas.isEmpty());
    double x1 = 0, x2 = 0;
    const bool rangeOk = numberExpr(m_from->text(), x1) && numberExpr(m_to->text(), x2) && x1 != x2;
    QString info;
    if (d.points == 0 && d.formulas.isEmpty()) {
        info = T("Введите хотя бы одну строку с числами.");
    } else if (d.points == 0) {
        info = T("Функций: %1").arg(d.formulas.size());
        if (!rangeOk)
            d.errors << T("Задайте отрезок X для функций");
    } else {
        const QString type = m_type->currentData().toString();
        int nsets = 0;
        for (const Table &t : d.blocks)
            nsets += (type == "auto" && t.cols.size() > 2) ? t.cols.size() - 1 : 1;
        if (replace)
            nsets = 1;
        info = T("Точек: %1, столбцов: %2 → наборов: %3").arg(d.points).arg(d.maxCols).arg(nsets);
        if (!d.formulas.isEmpty() && !replace)
            info += "; " + T("функций: %1").arg(d.formulas.size());
    }
    if (!d.errors.isEmpty())
        info += "<br><span style='color:#d33'>⚠︎ " + d.errors.mid(0, 3).join("<br>⚠︎ ").toHtmlEscaped().replace("&lt;br&gt;⚠︎", "<br>⚠︎")
                + (d.errors.size() > 3 ? QString("<br>… (%1)").arg(d.errors.size()) : QString()) + "</span>";
    m_info->setText(info);
    m_ok->setEnabled(d.points > 0 || (!d.formulas.isEmpty() && rangeOk));
}

QVector<Sel> TextDataDialog::apply()
{
    const TextData d = parseDataText(m_text->toPlainText());
    const QString type = m_type->currentData().toString();
    const bool replace = m_target->currentData().toInt() == 1;
    const bool fit = m_fit->isChecked();
    double x1 = 0, x2 = 1;
    numberExpr(m_from->text(), x1);
    numberExpr(m_to->text(), x2);
    const int n = m_npts->value();
    QVector<Sel> out;
    if (replace) {
        const Sel r = m_replace;
        m_doc->modify(T("Данные набора"), [&](Project &pr) {
            Graph *g = pr.graph(r.gid);
            DataSet *s = g ? g->set(r.sid) : nullptr;
            if (!s)
                return;
            if (d.blocks.isEmpty() && !d.formulas.isEmpty()) {
                Graph tmp;
                const QVector<int> id = addFormulaSets(tmp, d.formulas.mid(0, 1), x1, x2, n);
                if (id.isEmpty())
                    return;
                s->setType("xy");
                s->cols = tmp.set(id[0])->cols;
            } else if (!replaceSetFromText(*s, d, type)) {
                return;
            }
            if (fit)
                ensureVisible(*g, {r.sid});
            out << r;
        });
        return out;
    }
    int gid = m_graph->currentData().toInt();
    const QString legend = unicodeToGrace(m_legend->text().trimmed());
    m_doc->modify(T("Набор из текста"), [&](Project &pr) {
        if (gid < 0 || !pr.graph(gid)) {
            Graph ng(pr.nextGraphId());
            if (!pr.graphs.isEmpty()) {
                ng = pr.graphs.first();
                ng.id = pr.nextGraphId();
                ng.sets.clear();
                ng.p.set("title", QString());
                ng.p.set("subtitle", QString());
            }
            pr.graphs.append(ng);
            gid = ng.id;
        }
        Graph &g = *pr.graph(gid);
        QVector<int> ids = addSetsFromText(g, d, type, legend);
        if (x1 != x2)
            ids += addFormulaSets(g, d.formulas, x1, x2, n, legend);
        if (fit)
            ensureVisible(g, ids);
        for (int id : ids)
            out << Sel::set(gid, id);
    });
    return out;
}

// =============================================================================
// Операции над наборами

SetMathDialog::SetMathDialog(Document *doc, const QString &op0, QWidget *parent) : QDialog(parent), m_doc(doc)
{
    setWindowTitle(T("Операции над наборами"));
    resize(620, 440);
    auto *lay = new QVBoxLayout(this);
    m_form = new QFormLayout();
    m_op = new QComboBox(this);
    const QVector<QPair<QString, QString>> ops = {
        {"expr", T("Формула из наборов: s0.y + s1.y − s2.y …")},
        {"", ""},
        {"add", T("A + B — сложить")},
        {"sub", T("A − B — вычесть")},
        {"mul", T("A × B — умножить")},
        {"div", T("A ÷ B — разделить")},
        {"pow", T("A ^ B — возвести в степень")},
        {"", ""},
        {"sum", T("Сумма выделенных наборов")},
        {"mean", T("Среднее выделенных наборов")},
        {"", ""},
        {"neg", T("−A — сменить знак")},
        {"abs", T("|A| — модуль")},
        {"inv", T("1 / A — обратная величина")},
        {"normmax", T("Нормировать на максимум (max |y| = 1)")},
        {"normarea", T("Нормировать на площадь (∫ y dx = 1)")},
        {"submin", T("Вычесть минимум (min y = 0)")},
        {"swapxy", T("Поменять местами X и Y")},
    };
    for (const auto &o : ops) {
        if (o.first.isEmpty())
            m_op->insertSeparator(m_op->count());
        else
            m_op->addItem(o.second, o.first);
    }
    m_form->addRow(T("Операция"), m_op);

    // --- формула из наборов
    m_exprBox = new QWidget(this);
    {
        auto *v = new QVBoxLayout(m_exprBox);
        v->setContentsMargins(0, 0, 0, 0);
        m_expr = new QLineEdit(m_exprBox);
        QFont mono("Menlo");
        mono.setStyleHint(QFont::Monospace);
        m_expr->setFont(mono);
        m_expr->setPlaceholderText("s0.y + s1.y - s2.y");
        m_expr->setClearButtonEnabled(true);
        v->addWidget(m_expr);
        auto *ops = new QHBoxLayout();
        ops->setSpacing(4);
        m_insertSet = new QToolButton(m_exprBox);
        m_insertSet->setText(T("Набор"));
        m_insertSet->setToolTip(T("Вставить ссылку на набор"));
        m_insertSet->setPopupMode(QToolButton::InstantPopup);
        m_insertSet->setMenu(new QMenu(m_insertSet));
        ops->addWidget(m_insertSet);
        const QVector<QPair<QString, QString>> sym = {{"+", " + "}, {"−", " - "}, {"×", " * "}, {"÷", " / "},
                                                      {"(", "("},   {")", ")"},   {"x", "x"}};
        for (const auto &o : sym) {
            auto *b = new QToolButton(m_exprBox);
            b->setText(o.first);
            b->setMinimumWidth(28);
            const QString ins = o.second;
            connect(b, &QToolButton::clicked, this, [this, ins] { insertText(ins, ins == "x"); });
            ops->addWidget(b);
        }
        ops->addStretch(1);
        v->addLayout(ops);
        auto *tl = new QHBoxLayout();
        tl->setSpacing(4);
        const QVector<QPair<QString, QString>> templ = {{"sum", T("Σ Сумма выделенных")},
                                                        {"diff", T("Разность выделенных")},
                                                        {"mean", T("Среднее выделенных")}};
        for (const auto &t : templ) {
            auto *b = new QPushButton(t.second, m_exprBox);
            b->setProperty("kind", t.first);
            b->setAutoDefault(false);
            connect(b, &QPushButton::clicked, this, [this, k = t.first] {
                QVector<QPair<int, int>> v;
                for (const Sel &x : m_selected)
                    v << qMakePair(x.gid, x.sid);
                m_expr->setText(setExpressionFor(v, m_home, k));
                m_expr->setFocus();
            });
            m_templ << b;
            tl->addWidget(b);
        }
        tl->addStretch(1);
        v->addLayout(tl);
        m_exprHint = hintLabel(QString(), m_exprBox);
        v->addWidget(m_exprHint);
    }
    m_form->addRow(T("Формула"), m_exprBox);
    m_grid = new QComboBox(this);
    m_form->addRow(T("Точки X"), m_grid);

    m_a = new QComboBox(this);
    m_form->addRow(T("Набор A"), m_a);

    m_bRow = new QWidget(this);
    {
        auto *g = new QGridLayout(m_bRow);
        g->setContentsMargins(0, 0, 0, 0);
        m_useSet = new QRadioButton(T("набор"), m_bRow);
        m_useNum = new QRadioButton(T("число"), m_bRow);
        m_b = new QComboBox(m_bRow);
        m_num = new QLineEdit("2", m_bRow);
        m_num->setPlaceholderText(T("например, 2.5, -1e3 или pi/2"));
        m_axis = new QComboBox(m_bRow);
        m_axis->addItem(T("к Y"), false);
        m_axis->addItem(T("к X"), true);
        g->addWidget(m_useSet, 0, 0);
        g->addWidget(m_b, 0, 1, 1, 2);
        g->addWidget(m_useNum, 1, 0);
        g->addWidget(m_num, 1, 1);
        g->addWidget(m_axis, 1, 2);
        g->addWidget(hintLabel(T("A — первый набор, B — второй набор или число. Если точки X наборов не совпадают, "
                                 "B пересчитывается на X набора A линейной интерполяцией. Погрешности по Y учитываются."),
                               m_bRow),
                     2, 0, 1, 3);
        g->setColumnStretch(1, 1);
        m_useSet->setChecked(true);
    }
    m_form->addRow("B", m_bRow);
    m_many = new QLabel(this);
    m_many->setWordWrap(true);
    m_form->addRow(T("Наборы"), m_many);
    m_all = new QCheckBox(this);
    m_form->addRow(QString(), m_all);
    m_std = new QCheckBox(T("Погрешность — стандартное отклонение (σ)"), this);
    m_std->setChecked(true);
    m_form->addRow(QString(), m_std);
    m_result = new QComboBox(this);
    m_result->addItem(T("Новый набор"), false);
    m_result->addItem(T("Изменить исходный набор"), true);
    m_form->addRow(T("Результат"), m_result);
    lay->addLayout(m_form);

    m_preview = new QLabel(this);
    m_preview->setWordWrap(true);
    m_preview->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_preview->setMinimumHeight(48);
    m_preview->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    lay->addWidget(m_preview, 1);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Close, this);
    m_run = bb->addButton(T("Выполнить"), QDialogButtonBox::ApplyRole);
    m_run->setDefault(true);
    connect(m_run, &QPushButton::clicked, this, [this] { apply(); });
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(bb);

    // начальные значения: выделенные наборы — A и B
    QString op = op0;
    if (op.endsWith(":num")) {
        op.chop(4);
        m_useNum->setChecked(true);
    }
    fillSets();
    const int oi = m_op->findData(op.isEmpty() ? QString("expr") : op);
    m_op->setCurrentIndex(qMax(0, oi));
    {
        // формула по выделению: несколько наборов — сумма, один — он сам
        QVector<QPair<int, int>> v;
        for (const Sel &x : m_selected)
            v << qMakePair(x.gid, x.sid);
        m_expr->setText(setExpressionFor(v, m_home, "sum"));
    }
    if (m_selected.size() >= 2)
        m_b->setCurrentIndex(qMax(0, m_b->findData(QPoint(m_selected[1].gid, m_selected[1].sid))));
    else if (m_b->count() > 1 && m_b->currentIndex() == m_a->currentIndex())
        m_b->setCurrentIndex((m_a->currentIndex() + 1) % m_b->count());
    m_all->setChecked(m_selected.size() >= 2);

    connect(m_expr, &QLineEdit::textChanged, this, &SetMathDialog::update);
    for (QComboBox *c : {m_op, m_a, m_b, m_axis, m_result, m_grid})
        connect(c, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SetMathDialog::update);
    for (QAbstractButton *b : std::initializer_list<QAbstractButton *>{m_useSet, m_useNum, m_all, m_std})
        connect(b, &QAbstractButton::toggled, this, &SetMathDialog::update);
    connect(m_num, &QLineEdit::textChanged, this, [this] {
        m_useNum->setChecked(true);
        update();
    });
    connect(m_doc, &Document::changed, this, [this] {
        fillSets();
        update();
    });
    connect(m_doc, &Document::selectionChanged, this, [this] {
        fillSets();
        update();
    });
    update();
}

void SetMathDialog::setExpression(const QString &text)
{
    m_op->setCurrentIndex(qMax(0, m_op->findData("expr")));
    m_expr->setText(text);
}

void SetMathDialog::insertText(const QString &t, bool asOperand)
{
    // ссылку после другой ссылки или «)» отделяем плюсом: «s0.y» + «s1.y» → «s0.y + s1.y»
    const QString before = m_expr->text().left(m_expr->cursorPosition()).trimmed();
    if (asOperand && !before.isEmpty()) {
        const QChar c = before.back();
        if (c.isLetterOrNumber() || c == ')' || c == '.')
            m_expr->insert(" + ");
    }
    m_expr->insert(t);
    m_expr->setFocus();
}

void SetMathDialog::fillSets()
{
    m_selected = m_doc->selectedSets();
    m_home = m_selected.isEmpty() ? m_doc->currentGraph() : m_selected[0].gid;
    // меню «Набор»: ссылки на все наборы
    QMenu *menu = m_insertSet->menu();
    menu->clear();
    for (const Graph &g : m_doc->project().graphs) {
        for (const DataSet &s : g.sets) {
            const QString ref = g.id == m_home ? QString("s%1.y").arg(s.id) : QString("g%1.s%2.y").arg(g.id).arg(s.id);
            menu->addAction(QString("%1   G%2.%3").arg(ref, QString::number(g.id), s.displayName()), this,
                            [this, ref] { insertText(ref, true); });
        }
    }
    m_exprHint->setText(T("sN — набор N графика G%1 (то же, что sN.y), g1.s0 — набор другого графика; sN.x — его X. "
                          "Можно числа, x и функции: sin, exp, sqrt, abs… Если X наборов различаются, они пересчитываются "
                          "на точки X первого набора формулы.")
                            .arg(m_home));
    {
        const QVariant cur = m_grid->currentData();
        QSignalBlocker b(m_grid);
        m_grid->clear();
        m_grid->addItem(T("как у первого набора формулы"), QPoint(-1, -1));
        for (const Graph &g : m_doc->project().graphs)
            for (const DataSet &s : g.sets)
                m_grid->addItem(T("как у G%1.%2").arg(g.id).arg(s.displayName()), QPoint(g.id, s.id));
        m_grid->setCurrentIndex(qMax(0, cur.isValid() ? m_grid->findData(cur) : 0));
    }
    for (QComboBox *c : {m_a, m_b}) {
        const QVariant cur = c->currentData();
        QSignalBlocker b(c);
        c->clear();
        for (const Graph &g : m_doc->project().graphs)
            for (const DataSet &s : g.sets)
                c->addItem(QString("G%1.%2").arg(g.id).arg(s.displayName()), QPoint(g.id, s.id));
        int idx = cur.isValid() ? c->findData(cur) : -1;
        if (idx < 0 && c == m_a && !m_selected.isEmpty())
            idx = c->findData(QPoint(m_selected[0].gid, m_selected[0].sid));
        c->setCurrentIndex(qMax(0, idx));
    }
}

SetMathSpec SetMathDialog::spec() const
{
    SetMathSpec sp;
    sp.op = m_op->currentData().toString();
    const QPoint a = m_a->currentData().toPoint(), b = m_b->currentData().toPoint();
    sp.ga = a.x();
    sp.sa = m_a->count() ? a.y() : -1;
    sp.gb = b.x();
    sp.sb = m_b->count() ? b.y() : -1;
    sp.withSet = m_useSet->isChecked();
    sp.number = qQNaN();
    numberExpr(m_num->text(), sp.number);
    sp.onX = m_axis->currentData().toBool();
    sp.withStd = m_std->isChecked();
    sp.replace = m_result->currentData().toBool();
    const bool aggregate = isAggregateOp(sp.op);
    const bool perSet = !aggregate && !(isBinaryMathOp(sp.op) && sp.withSet);
    if (aggregate || (perSet && m_all->isChecked() && m_selected.size() >= 2))
        for (const Sel &s : m_selected)
            sp.many << qMakePair(s.gid, s.sid);
    if (aggregate)
        sp.replace = false;
    if (sp.op == "expr") {
        sp.expr = m_expr->text();
        sp.ga = m_home;
        const QPoint gp = m_grid->currentData().toPoint();
        sp.gridG = gp.x();
        sp.gridS = gp.y();
        sp.replace = false;
        sp.many.clear();
    }
    return sp;
}

void SetMathDialog::update()
{
    const SetMathSpec sp = spec();
    const bool binary = isBinaryMathOp(sp.op), aggregate = isAggregateOp(sp.op);
    const bool perSet = !aggregate && !(binary && sp.withSet);
    const bool many = m_selected.size() >= 2;
    const bool expr = sp.op == "expr";
    m_form->setRowVisible(m_exprBox, expr);
    m_form->setRowVisible(m_grid, expr);
    m_form->setRowVisible(m_bRow, binary);
    m_form->setRowVisible(m_a, !aggregate && !expr);
    m_form->setRowVisible(m_many, aggregate);
    m_form->setRowVisible(m_all, perSet && many && !expr);
    m_form->setRowVisible(m_std, sp.op == "mean");
    m_form->setRowVisible(m_result, !aggregate && !expr);
    for (QPushButton *b : m_templ)
        b->setEnabled(b->property("kind").toString() == "sum" ? !m_selected.isEmpty() : many);
    m_a->setEnabled(!(perSet && many && m_all->isChecked()));
    m_b->setEnabled(sp.withSet);
    m_axis->setEnabled(!sp.withSet);
    m_all->setText(T("Ко всем выделенным наборам (%1)").arg(m_selected.size()));
    m_result->setItemText(1, sp.many.size() > 1 ? T("Изменить исходные наборы") : T("Изменить исходный набор"));
    QStringList names;
    for (const Sel &s : m_selected)
        names << QString("G%1.S%2").arg(s.gid).arg(s.sid);
    m_many->setText(names.size() >= 2 ? names.join(", ")
                                      : T("Выделите два набора или больше (⌘‑щелчок на графике или в панели проекта)"));

    // предпросмотр: выполнить на копии проекта
    Project copy = m_doc->project();
    qint64 total = 0;
    for (const Graph &g : copy.graphs)
        for (const DataSet &s : g.sets)
            total += s.size();
    QString text;
    bool ok = true;
    if (expr && sp.expr.trimmed().isEmpty()) {
        ok = false;
        text = T("Введите формулу, например s0.y + s1.y");
    } else if (binary && !sp.withSet && !std::isfinite(sp.number)) {
        ok = false;
        text = "⚠︎ " + T("Число задано неверно");
    } else if (total < 500000) {
        const SetMathResult r = runSetMath(copy, sp);
        ok = r.ok;
        if (r.ok) {
            QStringList res;
            for (const auto &p : r.sets)
                if (const Graph *g = copy.graph(p.first))
                    if (const DataSet *s = g->set(p.second))
                        res << (sp.replace ? QString("S%1").arg(s->id) : QString("S%1 = %2").arg(s->id).arg(s->p.s("comment")));
            text = (sp.replace ? T("Изменятся: %1") : res.size() > 1 ? T("Будут созданы: %1") : T("Будет создан: %1")).arg(res.join(", ")).toHtmlEscaped();
            if (!r.message.isEmpty())
                text += "<br><span style='color:gray'>" + r.message.toHtmlEscaped() + "</span>";
        } else {
            text = "<span style='color:#d33'>⚠︎ " + r.message.toHtmlEscaped() + "</span>";
        }
    }
    m_preview->setText(text);
    m_run->setEnabled(ok);
}

QVector<Sel> SetMathDialog::apply()
{
    const SetMathSpec sp = spec();
    Project result = m_doc->project();
    const SetMathResult r = runSetMath(result, sp);
    QVector<Sel> out;
    if (!r.ok) {
        m_preview->setText("<span style='color:#d33'>⚠︎ " + r.message.toHtmlEscaped() + "</span>");
        return out;
    }
    const QString label = setMathLabel(m_doc->project(), sp, sp.many.isEmpty() ? sp.ga : sp.many[0].first,
                                       sp.many.isEmpty() ? sp.sa : sp.many[0].second);
    m_doc->modify(T("Операция: %1").arg(label), [&](Project &pr) { pr = result; });
    for (const auto &p : r.sets)
        out << Sel::set(p.first, p.second);
    // выделение не меняем: с теми же наборами удобно сделать следующую операцию
    return out;
}

// =============================================================================
// Галерея стилей

// Пример для пустого проекта: несколько кривых, точки с погрешностями, подписи
static Project galleryDemo()
{
    Project pr = newProject();
    Graph &g = pr.graphs[0];
    QString err;
    createFromFormula(g, 0, 10, 120, "sin(x) + 0.2*x", &err);
    createFromFormula(g, 0, 10, 120, "0.6*cos(1.5*x) + 1", &err);
    createFromFormula(g, 0, 10, 120, "exp(-x/4)*2 - 1", &err);
    DataSet &d = g.newSet("xydy");
    for (int i = 0; i <= 10; ++i) {
        const double x = i;
        d.cols[0] << x;
        d.cols[1] << 0.25 * x - 0.6 + 0.3 * std::sin(3 * x);
        d.cols[2] << 0.25;
    }
    d.p.set("line_type", 0);
    d.p.set("symbol", 2);
    d.p.set("symbol_size", 0.8);
    const char *legends[] = {"signal", "reference", "decay", "data"};
    for (int k = 0; k < g.sets.size(); ++k)
        g.sets[k].p.set("legend", legends[k]);
    g.p.set("legend", true);
    g.p.set("title", "Title");
    g.axes[0].p.set("label", "Time, s");
    g.axes[1].p.set("label", "Amplitude");
    autoscaleGraph(g);
    return pr;
}

StyleGalleryDialog::StyleGalleryDialog(Document *doc, QWidget *parent) : QDialog(parent), m_doc(doc)
{
    setWindowTitle(T("Галерея стилей"));
    auto *lay = new QVBoxLayout(this);
    lay->addWidget(hintLabel(T("Так будет выглядеть ваш проект. Щёлкните по стилю, чтобы применить его ко всем "
                               "графикам; отменить — ⌘Z."),
                             this));
    auto *top = new QHBoxLayout();
    top->addWidget(new QLabel(T("Палитра"), this));
    m_palette = new QComboBox(this);
    m_palette->addItem(T("как в стиле"), QString());
    for (const PaletteDef &pd : paletteDefs())
        m_palette->addItem(T(pd.name), pd.key);
    top->addWidget(m_palette);
    top->addStretch(1);
    lay->addLayout(top);
    auto *grid = new QGridLayout();
    grid->setSpacing(8);
    int k = 0;
    for (const StyleDef &sd : styleDefs()) {
        auto *b = new QToolButton(this);
        b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        b->setText(T(sd.name));
        b->setToolTip(T(sd.tip));
        b->setIconSize(QSize(200, 155));
        b->setAutoRaise(true);
        b->setProperty("style", sd.key);
        const QString key = sd.key;
        connect(b, &QToolButton::clicked, this, [this, key] { choose(key); });
        grid->addWidget(b, k / 5, k % 5);
        m_buttons << b;
        ++k;
    }
    lay->addLayout(grid);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(bb);
    connect(m_palette, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &StyleGalleryDialog::render);
    render();
}

Project StyleGalleryDialog::styled(const QString &key) const
{
    bool empty = true;
    for (const Graph &g : m_doc->project().graphs)
        empty = empty && g.sets.isEmpty();
    Project pr = empty ? galleryDemo() : m_doc->project();
    applyStyle(pr, key);
    const QString pal = m_palette->currentData().toString();
    if (const auto *c = paletteColors(pal))
        pr.applyPalette(*c);
    return pr;
}

void StyleGalleryDialog::render()
{
    const qreal dpr = devicePixelRatioF();
    for (QToolButton *b : m_buttons) {
        const Project pr = styled(b->property("style").toString());
        const QSize box = b->iconSize() * dpr;
        const double dpi = 72.0 * qMin(box.width() / double(pr.pageW), box.height() / double(pr.pageH));
        QImage img = renderImage(pr, dpi);
        // тонкая рамка вокруг страницы — видно границы светлого листа
        QPainter p(&img);
        p.setPen(QColor(0, 0, 0, 40));
        p.drawRect(img.rect().adjusted(0, 0, -1, -1));
        p.end();
        img.setDevicePixelRatio(dpr);
        b->setIcon(QIcon(QPixmap::fromImage(img)));
    }
}

void StyleGalleryDialog::choose(const QString &key)
{
    m_chosen = key;
    QString name = key;
    for (const StyleDef &sd : styleDefs())
        if (sd.key == key)
            name = T(sd.name);
    const QString pal = m_palette->currentData().toString();
    m_doc->modify(T("Стиль: %1").arg(name), [&](Project &pr) {
        applyStyle(pr, key);
        if (const auto *c = paletteColors(pal))
            pr.applyPalette(*c);
    });
    accept();
}

// =============================================================================

ArrangeDialog::ArrangeDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(T("Расставить графики"));
    auto *f = new QFormLayout(this);
    m_rows = new QSpinBox(this);
    m_cols = new QSpinBox(this);
    m_rows->setRange(1, 20);
    m_cols->setRange(1, 20);
    m_rows->setValue(2);
    m_cols->setValue(1);
    f->addRow(T("Строк"), m_rows);
    f->addRow(T("Столбцов"), m_cols);
    auto mk = [&](const QString &l, double v) {
        auto *sb = new QDoubleSpinBox(this);
        sb->setRange(0, 1);
        sb->setDecimals(3);
        sb->setSingleStep(0.01);
        sb->setValue(v);
        f->addRow(l, sb);
        return sb;
    };
    m_left = mk(T("Поле слева"), 0.15);
    m_right = mk(T("Поле справа"), 0.05);
    m_top = mk(T("Поле сверху"), 0.08);
    m_bottom = mk(T("Поле снизу"), 0.12);
    m_hgap = mk(T("Промежуток по горизонтали"), 0.1);
    m_vgap = mk(T("Промежуток по вертикали"), 0.1);
    f->addRow(hintLabel(T("Недостающие графики будут созданы с оформлением первого."), this));
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    f->addRow(bb);
}
int ArrangeDialog::rows() const { return m_rows->value(); }
int ArrangeDialog::cols() const { return m_cols->value(); }
double ArrangeDialog::hgap() const { return m_hgap->value(); }
double ArrangeDialog::vgap() const { return m_vgap->value(); }
double ArrangeDialog::left() const { return m_left->value(); }
double ArrangeDialog::right() const { return m_right->value(); }
double ArrangeDialog::top() const { return m_top->value(); }
double ArrangeDialog::bottom() const { return m_bottom->value(); }

// =============================================================================

ExportDialog::ExportDialog(const QString &suggested, QWidget *parent) : QDialog(parent)
{
    setWindowTitle(T("Экспорт изображения"));
    auto *f = new QFormLayout(this);
    m_fmt = new QComboBox(this);
    m_fmt->addItem(T("PDF — вектор, для статей"), "pdf");
    m_fmt->addItem(T("SVG — вектор, для правки"), "svg");
    m_fmt->addItem(T("PNG — картинка"), "png");
    m_fmt->addItem(T("JPEG — картинка"), "jpg");
    m_fmt->addItem(T("TIFF — картинка"), "tiff");
    m_fmt->setCurrentIndex(qMax(0, m_fmt->findData(suggested)));
    f->addRow(T("Формат"), m_fmt);
    m_dpi = new QSpinBox(this);
    m_dpi->setRange(36, 2400);
    m_dpi->setValue(300);
    m_dpi->setSuffix(" dpi");
    f->addRow(T("Разрешение"), m_dpi);
    m_transparent = new QCheckBox(T("Прозрачный фон"), this);
    f->addRow(QString(), m_transparent);
    auto upd = [this] {
        const QString k = format();
        m_dpi->setEnabled(k != "pdf" && k != "svg");
    };
    connect(m_fmt, QOverload<int>::of(&QComboBox::currentIndexChanged), this, upd);
    upd();
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    bb->button(QDialogButtonBox::Ok)->setText(T("Сохранить…"));
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    f->addRow(bb);
}
QString ExportDialog::format() const { return m_fmt->currentData().toString(); }
double ExportDialog::dpi() const { return m_dpi->value(); }
bool ExportDialog::transparent() const { return m_transparent->isChecked(); }

// =============================================================================

TickSpecDialog::TickSpecDialog(Document *doc, int gid, int axis, QWidget *parent)
    : QDialog(parent), m_doc(doc), m_gid(gid), m_axis(axis)
{
    setWindowTitle(T("Особые деления"));
    resize(420, 460);
    auto *l = new QVBoxLayout(this);
    m_mode = new QComboBox(this);
    m_mode->addItem(T("Свои положения и подписи"), "both");
    m_mode->addItem(T("Только свои положения"), "ticks");
    m_mode->addItem(T("Выключить (обычные деления)"), "none");
    l->addWidget(m_mode);
    m_table = new QTableWidget(0, 3, this);
    m_table->setHorizontalHeaderLabels({T("Значение"), T("Основное"), T("Подпись")});
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    l->addWidget(m_table, 1);
    const Graph *g = doc->project().graph(gid);
    if (g) {
        const Axis &a = g->axes[axis];
        m_mode->setCurrentIndex(qMax(0, m_mode->findData(a.p.s("tick_spec_type") == "none" ? "both" : a.p.s("tick_spec_type"))));
        QVector<TickSpec> spec = a.spec;
        if (spec.isEmpty())  // начать с текущих автоматических делений
            for (const Tick &t : computeTicks(*g, axis))
                spec.append({t.major, t.w, t.label, true});
        for (const auto &t : spec) {
            const int r = m_table->rowCount();
            m_table->insertRow(r);
            m_table->setItem(r, 0, new QTableWidgetItem(QString::number(t.value, 'g', 12)));
            auto *chk = new QTableWidgetItem();
            chk->setCheckState(t.major ? Qt::Checked : Qt::Unchecked);
            m_table->setItem(r, 1, chk);
            m_table->setItem(r, 2, new QTableWidgetItem(t.label));
        }
    }
    auto *row = new QHBoxLayout();
    auto *add = new QPushButton(icon("plus"), T("Добавить"), this);
    auto *del = new QPushButton(icon("minus"), T("Удалить"), this);
    connect(add, &QPushButton::clicked, this, [this] {
        const int r = m_table->rowCount();
        m_table->insertRow(r);
        m_table->setItem(r, 0, new QTableWidgetItem("0"));
        auto *chk = new QTableWidgetItem();
        chk->setCheckState(Qt::Checked);
        m_table->setItem(r, 1, chk);
        m_table->setItem(r, 2, new QTableWidgetItem(""));
    });
    connect(del, &QPushButton::clicked, this, [this] {
        const int r = m_table->currentRow();
        if (r >= 0)
            m_table->removeRow(r);
    });
    row->addWidget(add);
    row->addWidget(del);
    row->addStretch(1);
    l->addLayout(row);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    l->addWidget(bb);
}

void TickSpecDialog::apply()
{
    QVector<TickSpec> spec;
    for (int r = 0; r < m_table->rowCount(); ++r) {
        TickSpec t;
        t.value = m_table->item(r, 0) ? m_table->item(r, 0)->text().replace(',', '.').toDouble() : 0;
        t.major = m_table->item(r, 1) && m_table->item(r, 1)->checkState() == Qt::Checked;
        t.label = m_table->item(r, 2) ? unicodeToGrace(m_table->item(r, 2)->text()) : QString();
        spec << t;
    }
    std::stable_sort(spec.begin(), spec.end(), [](const TickSpec &a, const TickSpec &b) { return a.value < b.value; });
    const QString mode = m_mode->currentData().toString();
    const int gid = m_gid, axis = m_axis;
    m_doc->modify(T("Особые деления"), [&](Project &pr) {
        if (Graph *g = pr.graph(gid)) {
            g->axes[axis].spec = spec;
            g->axes[axis].p.set("tick_spec_type", mode);
        }
    });
}

// =============================================================================

TextDialog::TextDialog(Document *doc, const QString &title, const QString &text, int font, QWidget *parent)
    : QDialog(parent), m_doc(doc), m_font(font)
{
    setWindowTitle(title);
    resize(560, 260);
    auto *l = new QVBoxLayout(this);
    m_edit = new QLineEdit(text, this);
    QFont mono("Menlo");
    mono.setStyleHint(QFont::Monospace);
    m_edit->setFont(mono);
    l->addWidget(m_edit);
    auto *tools = new QHBoxLayout();
    struct B { const char *label; const char *ins; const char *tip; };
    static const B btns[] = {
        {"x²", "\\S\\N", "Верхний индекс: \\S…\\N"}, {"x₂", "\\s\\N", "Нижний индекс: \\s…\\N"},
        {"α", "\\xa\\f{}", "Греческая буква: \\x + латинская"}, {"β", "\\xb\\f{}", ""}, {"γ", "\\xg\\f{}", ""},
        {"δ", "\\xd\\f{}", ""}, {"λ", "\\xl\\f{}", ""}, {"μ", "\\xm\\f{}", ""}, {"σ", "\\xs\\f{}", ""},
        {"Δ", "\\xD\\f{}", ""}, {"Ω", "\\xW\\f{}", ""}, {"°", "\\#{b0}", "Градус"}, {"±", "\\x\\#{b1}\\f{}", ""},
        {"×", "\\x\\#{b4}\\f{}", ""}, {"Å", "\\#{c5}", "Ангстрем"}, {"x̄", "\\ox\\O", "Черта сверху"},
    };
    for (const auto &b : btns) {
        auto *t = new QToolButton(this);
        t->setText(T(b.label));
        if (*b.tip)
            t->setToolTip(T(b.tip));
        const QString ins = QString::fromLatin1(b.ins);
        connect(t, &QToolButton::clicked, this, [this, ins] { insert(ins); });
        tools->addWidget(t);
    }
    tools->addStretch(1);
    l->addLayout(tools);
    m_preview = new QLabel(this);
    m_preview->setMinimumHeight(90);
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setObjectName("textPreview");
    l->addWidget(m_preview, 1);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    l->addWidget(bb);
    connect(m_edit, &QLineEdit::textChanged, this, &TextDialog::updatePreview);
    updatePreview();
    m_edit->setFocus();
    m_edit->selectAll();
}

QString TextDialog::text() const { return unicodeToGrace(m_edit->text()); }

void TextDialog::insert(const QString &s)
{
    // индексы оборачивают выделение
    QString sel = m_edit->selectedText();
    int pos = m_edit->hasSelectedText() ? m_edit->selectionStart() : m_edit->cursorPosition();
    QString t = m_edit->text();
    if (m_edit->hasSelectedText())
        t.remove(pos, sel.size());
    QString ins = s;
    int cursor = s.size();
    if (s == "\\S\\N" || s == "\\s\\N") {
        ins = s.left(2) + sel + "\\N";
        cursor = 2 + sel.size();
    } else if (s == "\\ox\\O") {
        ins = "\\o" + (sel.isEmpty() ? QString("x") : sel) + "\\O";
        cursor = ins.size();
    }
    t.insert(pos, ins);
    m_edit->setText(t);
    m_edit->setCursorPosition(pos + cursor);
    m_edit->setFocus();
}

void TextDialog::updatePreview()
{
    const double dpr = devicePixelRatioF();
    QImage probe(1, 1, QImage::Format_ARGB32_Premultiplied);
    probe.setDotsPerMeterX(int(72 / 0.0254));
    probe.setDotsPerMeterY(int(72 / 0.0254));
    const QRectF tb = TextEngine(m_doc->project(), &probe).bounds(unicodeToGrace(m_edit->text()), m_font, 26, JustCenter | JustMiddle);
    const QSize sz(qMax(qMax(200, m_preview->width() - 4), int(tb.width()) + 40), qMax(80, int(tb.height()) + 30));
    QImage img(sz * dpr, QImage::Format_ARGB32_Premultiplied);
    img.setDevicePixelRatio(dpr);
    const int dpm = int(72 / 0.0254);
    img.setDotsPerMeterX(dpm);
    img.setDotsPerMeterY(dpm);
    img.fill(Qt::white);
    {
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::TextAntialiasing);
        TextEngine te(m_doc->project(), &img);
        te.draw(&p, unicodeToGrace(m_edit->text()), m_font, 26, Qt::black, QPointF(sz.width() / 2.0, sz.height() / 2.0), 0,
                JustCenter | JustMiddle);
    }
    m_preview->setPixmap(QPixmap::fromImage(img));
}

}  // namespace ng
