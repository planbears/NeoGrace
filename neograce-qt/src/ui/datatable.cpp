#include "datatable.h"
#include "i18n.h"

#include "agrio.h"
#include "icons.h"
#include "ops.h"

#include <algorithm>
#include <QApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QSet>
#include <QTableView>
#include <QToolButton>
#include <QVBoxLayout>
#include <cmath>
#include <limits>

namespace ng {

SetModel::SetModel(Document *doc, QObject *parent) : QAbstractTableModel(parent), m_doc(doc)
{
    connect(doc, &Document::changed, this, &SetModel::reload);
}

const DataSet *SetModel::set() const
{
    const Graph *g = m_doc->project().graph(m_gid);
    return g ? g->set(m_sid) : nullptr;
}

void SetModel::setTarget(int gid, int sid)
{
    m_gid = gid;
    m_sid = sid;
    beginResetModel();
    const DataSet *s = set();
    m_rows = s ? s->size() : 0;
    m_cols = s ? s->ncols() : 0;
    m_strings = s && s->type() == "xystring";
    endResetModel();
}

void SetModel::reload()
{
    const DataSet *s = set();
    const int rows = s ? s->size() : 0;
    const int cols = s ? s->ncols() : 0;
    const bool str = s && s->type() == "xystring";
    if (rows != m_rows || cols != m_cols || str != m_strings) {
        beginResetModel();
        m_rows = rows;
        m_cols = cols;
        m_strings = str;
        endResetModel();
    } else if (rows > 0) {
        emit dataChanged(index(0, 0), index(rows - 1, columnCount({}) - 1));
    }
}

int SetModel::rowCount(const QModelIndex &p) const { return p.isValid() || !set() ? 0 : m_rows + 1; }
int SetModel::columnCount(const QModelIndex &p) const { return p.isValid() ? 0 : m_cols + (m_strings ? 1 : 0); }

QVariant SetModel::data(const QModelIndex &i, int role) const
{
    const DataSet *s = set();
    if (!s || !i.isValid())
        return {};
    if (role == Qt::TextAlignmentRole)
        return int(Qt::AlignRight | Qt::AlignVCenter);
    if (role != Qt::DisplayRole && role != Qt::EditRole)
        return {};
    if (i.row() >= s->size())
        return QString();
    if (i.column() >= m_cols)
        return s->strings.value(i.row());
    const double v = s->col(i.column()).value(i.row());
    if (std::isnan(v))
        return QStringLiteral("NaN");
    return role == Qt::EditRole ? QString::number(v, 'g', 15) : QString::number(v, 'g', 10);
}

QVariant SetModel::headerData(int section, Qt::Orientation o, int role) const
{
    if (role != Qt::DisplayRole)
        return {};
    if (o == Qt::Vertical)
        return section < m_rows ? QString::number(section) : QStringLiteral("+");
    const DataSet *s = set();
    if (!s)
        return {};
    if (section >= m_cols)
        return T("Строка");
    return setColumnNames(s->type()).value(section);
}

Qt::ItemFlags SetModel::flags(const QModelIndex &i) const
{
    return QAbstractTableModel::flags(i) | Qt::ItemIsEditable;
}

bool SetModel::setData(const QModelIndex &i, const QVariant &v, int role)
{
    if (role != Qt::EditRole || !set())
        return false;
    const int row = i.row(), col = i.column();
    const QString text = v.toString().trimmed();
    double d = 0;
    if (col < m_cols) {
        bool ok = false;
        d = text.toDouble(&ok);
        if (!ok) {
            QString t = text;
            t.replace(',', '.').replace(QChar(0x2212), '-');
            d = t.toDouble(&ok);
        }
        if (!ok) {
            if (text.compare("nan", Qt::CaseInsensitive) == 0 || text.isEmpty())
                d = std::numeric_limits<double>::quiet_NaN();
            else
                return false;
        }
    }
    const int gid = m_gid, sid = m_sid, ncols = m_cols;
    m_doc->modify(T("Правка данных"), [&](Project &pr) {
        Graph *g = pr.graph(gid);
        DataSet *s = g ? g->set(sid) : nullptr;
        if (!s)
            return;
        if (row >= s->size()) {
            const int n = s->size();
            s->resize(n + 1);
            for (int k = 0; k < ncols; ++k)
                s->cols[k][n] = k == col ? d : (n > 0 && k == 0 ? s->cols[0][n - 1] + 1 : 0.0);
        } else if (col < ncols) {
            s->cols[col][row] = d;
        }
        if (col >= ncols) {
            while (s->strings.size() < s->size())
                s->strings << QString();
            s->strings[row] = text;
        }
    });
    return true;
}

// ---------------------------------------------------------------------------

DataTable::DataTable(Document *doc, QWidget *parent) : QWidget(parent), m_doc(doc)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    auto *bar = new QWidget(this);
    auto *bl = new QHBoxLayout(bar);
    bl->setContentsMargins(8, 4, 8, 4);
    m_title = new QLabel(T("Выберите набор"), bar);
    QFont f = m_title->font();
    f.setWeight(QFont::DemiBold);
    m_title->setFont(f);
    bl->addWidget(m_title, 1);
    auto tb = [&](const QString &ic, const QString &tipText, auto fn) {
        auto *b = new QToolButton(bar);
        b->setIcon(icon(ic));
        b->setToolTip(tipText);
        b->setAutoRaise(true);
        connect(b, &QToolButton::clicked, this, fn);
        bl->addWidget(b);
        return b;
    };
    tb("plus", T("Вставить строку"), [this] { insertRow(); });
    tb("minus", T("Удалить выделенные строки"), [this] { deleteRows(); });
    tb("sort", T("Упорядочить по X"), [this] {
        const int gid = m_model->gid(), sid = m_model->sid();
        m_doc->modify(T("Сортировка"), [&](Project &pr) {
            if (Graph *g = pr.graph(gid))
                if (DataSet *s = g->set(sid))
                    sortSet(*s, false, false);
        });
    });
    tb("copy", T("Копировать (⌘C)"), [this] { copySelection(); });
    tb("paste", T("Вставить из буфера (⌘V) — можно из Excel/Numbers"), [this] { paste(); });
    lay->addWidget(bar);
    m_model = new SetModel(doc, this);
    m_view = new QTableView(this);
    m_view->setModel(m_model);
    m_view->setFrameShape(QFrame::NoFrame);
    m_view->setAlternatingRowColors(true);
    m_view->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_view->verticalHeader()->setDefaultSectionSize(22);
    m_view->setSelectionBehavior(QAbstractItemView::SelectItems);
    m_view->installEventFilter(this);
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_view, &QTableView::customContextMenuRequested, this, [this](const QPoint &p) {
        QMenu m;
        m.addAction(icon("copy"), T("Копировать"), this, &DataTable::copySelection);
        m.addAction(icon("paste"), T("Вставить"), this, &DataTable::paste);
        m.addSeparator();
        m.addAction(icon("plus"), T("Вставить строку"), this, &DataTable::insertRow);
        m.addAction(icon("minus"), T("Удалить строки"), this, &DataTable::deleteRows);
        m.exec(m_view->viewport()->mapToGlobal(p));
    });
    lay->addWidget(m_view);
    connect(doc, &Document::selectionChanged, this, [this] {
        const Sel &s = m_doc->selection();
        if (s.kind == Sel::Set && m_follow)
            showSet(s.gid, s.sid);
    });
    connect(doc, &Document::changed, this, &DataTable::updateTitle);
}

void DataTable::showSet(int gid, int sid)
{
    m_model->setTarget(gid, sid);
    updateTitle();
}

void DataTable::updateTitle()
{
    const DataSet *s = m_model->set();
    if (!s) {
        m_title->setText(T("Выберите набор"));
        return;
    }
    m_title->setText(T("G%1 · %2 · %3 точек").arg(m_model->gid()).arg(s->displayName()).arg(s->size()));
}

bool DataTable::eventFilter(QObject *o, QEvent *e)
{
    if (o == m_view && e->type() == QEvent::KeyPress) {
        auto *k = static_cast<QKeyEvent *>(e);
        if (k->matches(QKeySequence::Copy)) {
            copySelection();
            return true;
        }
        if (k->matches(QKeySequence::Paste)) {
            paste();
            return true;
        }
        if (k->key() == Qt::Key_Delete || k->key() == Qt::Key_Backspace) {
            if (m_view->selectionModel()->selectedRows().size() > 0) {
                deleteRows();
                return true;
            }
        }
    }
    return QWidget::eventFilter(o, e);
}

void DataTable::copySelection()
{
    const auto idx = m_view->selectionModel()->selectedIndexes();
    if (idx.isEmpty())
        return;
    int r0 = 1 << 30, r1 = -1, c0 = 1 << 30, c1 = -1;
    for (const auto &i : idx) {
        r0 = qMin(r0, i.row());
        r1 = qMax(r1, i.row());
        c0 = qMin(c0, i.column());
        c1 = qMax(c1, i.column());
    }
    QString out;
    for (int r = r0; r <= r1; ++r) {
        QStringList row;
        for (int c = c0; c <= c1; ++c)
            row << m_model->data(m_model->index(r, c), Qt::EditRole).toString();
        out += row.join('\t') + '\n';
    }
    QApplication::clipboard()->setText(out);
}

void DataTable::paste()
{
    const DataSet *s = m_model->set();
    if (!s)
        return;
    const QString text = QApplication::clipboard()->text();
    QString norm = text;
    if (norm.contains('\t'))  // из таблиц: десятичная запятая допустима
        norm.replace(',', '.').replace('\t', ' ');
    auto tables = readTables(norm, false);
    if (tables.isEmpty())
        return;
    const Table t = tables.first();
    QModelIndex cur = m_view->currentIndex();
    const int r0 = cur.isValid() ? cur.row() : 0, c0 = cur.isValid() ? cur.column() : 0;
    const int gid = m_model->gid(), sid = m_model->sid();
    m_doc->modify(T("Вставка данных"), [&](Project &pr) {
        Graph *g = pr.graph(gid);
        DataSet *d = g ? g->set(sid) : nullptr;
        if (!d)
            return;
        const int need = r0 + t.rows();
        if (d->size() < need)
            d->resize(need);
        for (int c = 0; c < t.cols.size() && c0 + c < d->cols.size(); ++c)
            for (int r = 0; r < t.rows(); ++r)
                d->cols[c0 + c][r0 + r] = t.cols[c][r];
    });
}

void DataTable::deleteRows()
{
    QSet<int> rows;
    for (const auto &i : m_view->selectionModel()->selectedIndexes())
        rows.insert(i.row());
    if (rows.isEmpty() && m_view->currentIndex().isValid())
        rows.insert(m_view->currentIndex().row());
    if (rows.isEmpty())
        return;
    const int gid = m_model->gid(), sid = m_model->sid();
    m_doc->modify(T("Удаление строк"), [&](Project &pr) {
        Graph *g = pr.graph(gid);
        DataSet *d = g ? g->set(sid) : nullptr;
        if (!d)
            return;
        QList<int> sorted = rows.values();
        std::sort(sorted.begin(), sorted.end(), std::greater<int>());
        for (int r : sorted) {
            if (r >= d->size())
                continue;
            for (auto &c : d->cols)
                c.remove(r);
            if (r < d->strings.size())
                d->strings.removeAt(r);
        }
    });
}

void DataTable::insertRow()
{
    const QModelIndex cur = m_view->currentIndex();
    const int gid = m_model->gid(), sid = m_model->sid();
    const DataSet *s0 = m_model->set();
    if (!s0)
        return;
    const int at = cur.isValid() ? qMin(cur.row(), s0->size()) : s0->size();
    m_doc->modify(T("Новая строка"), [&](Project &pr) {
        Graph *g = pr.graph(gid);
        DataSet *d = g ? g->set(sid) : nullptr;
        if (!d)
            return;
        for (auto &c : d->cols) {
            double v = at < c.size() ? c[at] : (c.isEmpty() ? 0.0 : c.last());
            c.insert(at, v);
        }
        if (!d->strings.isEmpty())
            d->strings.insert(at, QString());
    });
}

}  // namespace ng
