#include "projecttree.h"
#include "i18n.h"

#include "icons.h"
#include "ops.h"

#include <algorithm>
#include <QContextMenuEvent>
#include <QDropEvent>
#include <QHeaderView>
#include <QScrollBar>
#include <QSet>

namespace ng {

enum Role { KindRole = Qt::UserRole + 1, GidRole, SidRole, ObjRole };

ProjectTree::ProjectTree(Document *doc, QWidget *parent) : QTreeWidget(parent), m_doc(doc)
{
    setHeaderHidden(true);
    setColumnCount(1);
    setRootIsDecorated(true);
    setIndentation(14);
    setUniformRowHeights(true);
    setFrameShape(QFrame::NoFrame);
    setIconSize(QSize(16, 16));
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setContextMenuPolicy(Qt::CustomContextMenu);
    // перетаскивание меняет порядок слоёв и переносит наборы между графиками
    setDragEnabled(true);
    setAcceptDrops(true);
    setDropIndicatorShown(true);
    setDragDropMode(QAbstractItemView::InternalMove);
    setDefaultDropAction(Qt::MoveAction);
    setAttribute(Qt::WA_MacShowFocusRect, false);
    connect(doc, &Document::changed, this, &ProjectTree::rebuild);
    connect(doc, &Document::selectionChanged, this, &ProjectTree::syncSelection);
    connect(this, &QTreeWidget::itemSelectionChanged, this, [this] {
        if (m_updating)
            return;
        auto items = selectedItems();
        if (items.isEmpty())
            return;
        // несколько наборов — групповое выделение
        QVector<Sel> sets;
        for (auto *it : items) {
            const Sel s = selFor(it);
            if (s.kind == Sel::Set)
                sets << s;
        }
        if (sets.size() > 1) {
            m_doc->selectSets(sets, selFor(currentItem()));
            return;
        }
        QTreeWidgetItem *cur = currentItem() && currentItem()->isSelected() ? currentItem() : items.first();
        m_doc->select(selFor(cur));
    });
    connect(this, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *it, int) {
        if (m_updating)
            return;
        const Sel s = selFor(it);
        const bool on = it->checkState(0) == Qt::Checked;
        if (s.kind == Sel::Set) {
            m_doc->modify(on ? T("Показать набор") : T("Скрыть набор"), [&](Project &pr) {
                if (Graph *g = pr.graph(s.gid))
                    if (DataSet *d = g->set(s.sid))
                        d->p.set("hidden", !on);
            });
        } else if (s.kind == Sel::Graph) {
            m_doc->modify(on ? T("Показать график") : T("Скрыть график"), [&](Project &pr) {
                if (Graph *g = pr.graph(s.gid))
                    g->hp.set("hidden", !on);
            });
        } else if (s.kind == Sel::Object) {
            m_doc->modify(on ? T("Показать объект") : T("Скрыть объект"), [&](Project &pr) {
                if (s.obj >= 0 && s.obj < pr.objects.size())
                    pr.objects[s.obj].p.set("on", on);
            });
        }
    });
    connect(this, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QTreeWidgetItem *it = itemAt(pos);
        Sel s = it ? selFor(it) : Sel::page();
        if (it && !(s.kind == Sel::Set && m_doc->isSetSelected(s.gid, s.sid)))
            m_doc->select(s);
        emit contextMenuWanted(s, viewport()->mapToGlobal(pos));
    });
    connect(this, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *it, int) {
        emit propertiesWanted(selFor(it));
    });
    rebuild();
}

Sel ProjectTree::selFor(QTreeWidgetItem *it) const
{
    if (!it)
        return Sel();
    Sel s;
    s.kind = Sel::Kind(it->data(0, KindRole).toInt());
    s.gid = it->data(0, GidRole).toInt();
    s.sid = it->data(0, SidRole).toInt();
    s.obj = it->data(0, ObjRole).toInt();
    if (s.kind == Sel::Axis)
        s.axis = s.sid;
    if (s.kind != Sel::Set && s.kind != Sel::Axis)
        s.sid = -1;
    if (s.kind == Sel::Axis)
        s.sid = -1;
    if (s.kind != Sel::Object)
        s.obj = -1;
    if (s.kind == Sel::Page || s.kind == Sel::Object)
        s.gid = -1;
    return s;
}

static QTreeWidgetItem *mk(QTreeWidgetItem *parent, const QString &text, const QIcon &ic, Sel::Kind k, int gid = -1,
                           int sid = -1, int obj = -1)
{
    auto *it = new QTreeWidgetItem(parent);
    it->setText(0, text);
    it->setIcon(0, ic);
    it->setData(0, KindRole, int(k));
    it->setData(0, GidRole, gid);
    it->setData(0, SidRole, sid);
    it->setData(0, ObjRole, obj);
    return it;
}

void ProjectTree::rebuild()
{
    m_updating = true;
    // запомнить раскрытые графики
    QSet<int> collapsed;
    for (int i = 0; i < topLevelItemCount(); ++i) {
        QTreeWidgetItem *top = topLevelItem(i);
        for (int k = 0; k < top->childCount(); ++k) {
            QTreeWidgetItem *c = top->child(k);
            if (c->data(0, KindRole).toInt() == Sel::Graph && !c->isExpanded())
                collapsed.insert(c->data(0, GidRole).toInt());
        }
    }
    const int scroll = verticalScrollBar() ? verticalScrollBar()->value() : 0;
    clear();
    const Project &pr = m_doc->project();
    auto *page = new QTreeWidgetItem(this);
    page->setText(0, T("Страница  %1×%2").arg(pr.pageW).arg(pr.pageH));
    page->setIcon(0, icon("page"));
    page->setData(0, KindRole, int(Sel::Page));
    page->setFlags(page->flags() & ~(Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled));
    QFont bf = page->font(0);
    bf.setWeight(QFont::DemiBold);
    page->setFont(0, bf);
    for (const Graph &g : pr.graphs) {
        auto *gi = mk(page, g.displayName() + (g.type() != "XY" ? QString("  [%1]").arg(g.type()) : QString()),
                      icon("graph"), Sel::Graph, g.id);
        gi->setFlags(gi->flags() | Qt::ItemIsUserCheckable);
        gi->setCheckState(0, g.hp.b("hidden") ? Qt::Unchecked : Qt::Checked);
        if (!g.hp.b("on"))
            gi->setForeground(0, palette().color(QPalette::Disabled, QPalette::Text));
        for (const DataSet &s : g.sets) {
            QColor c = pr.color(s.p.i(s.p.i("line_type") != 0 && s.p.i("line_linestyle") != 0 ? "line_color" : "symbol_color"));
            auto *si = mk(gi, s.displayName() + QString("  (%1, %2)").arg(s.type()).arg(s.size()), colorIcon(c), Sel::Set,
                          g.id, s.id);
            si->setFlags(si->flags() | Qt::ItemIsUserCheckable);
            si->setCheckState(0, s.hidden() ? Qt::Unchecked : Qt::Checked);
            si->setToolTip(0, s.p.s("comment"));
        }
        gi->setExpanded(!collapsed.contains(g.id));
    }
    if (!pr.objects.isEmpty()) {
        auto *oi = new QTreeWidgetItem(page);
        oi->setText(0, T("Надписи и фигуры"));
        oi->setIcon(0, icon("shapes"));
        oi->setData(0, KindRole, int(Sel::Page));
        oi->setFlags(Qt::ItemIsEnabled | Qt::ItemIsDropEnabled);
        for (int i = 0; i < pr.objects.size(); ++i) {
            const DrawObject &o = pr.objects[i];
            static const QHash<QString, QString> ic = {{"string", "text"}, {"line", "line"}, {"box", "box"}, {"ellipse", "ellipse"}};
            QString name = o.displayName();
            if (o.p.s("loctype") == "world")
                name += QString("  → G%1").arg(o.graph);
            auto *it = mk(oi, name, icon(ic.value(o.kind, "shapes")), Sel::Object, -1, -1, i);
            it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
            it->setCheckState(0, o.p.b("on") ? Qt::Checked : Qt::Unchecked);
        }
        oi->setExpanded(pr.objects.size() < 30);
    }
    page->setExpanded(true);
    if (verticalScrollBar())
        verticalScrollBar()->setValue(scroll);
    m_updating = false;
    syncSelection();
}

void ProjectTree::dropEvent(QDropEvent *e)
{
    QTreeWidgetItem *target = itemAt(e->position().toPoint());
    const DropIndicatorPosition where = dropIndicatorPosition();
    e->setDropAction(Qt::IgnoreAction);  // порядок меняем сами, дерево перестроится
    e->accept();
    if (!target)
        return;
    const Sel t = selFor(target);
    const bool below = where == QAbstractItemView::BelowItem;
    QVector<Sel> sets;
    Sel other;
    for (auto *it : selectedItems()) {
        const Sel s = selFor(it);
        if (s.kind == Sel::Set)
            sets << s;
        else if (s.kind == Sel::Graph || s.kind == Sel::Object)
            other = s;
    }
    const Project &pr0 = m_doc->project();
    if (!sets.isEmpty()) {
        int tg = -1, idx = -1;
        if (t.kind == Sel::Set) {
            tg = t.gid;
            const Graph *g = pr0.graph(tg);
            for (int k = 0; g && k < g->sets.size(); ++k)
                if (g->sets[k].id == t.sid)
                    idx = k + (below ? 1 : 0);
        } else if (t.kind == Sel::Graph) {
            tg = t.gid;
        } else {
            return;
        }
        const Graph *g = pr0.graph(tg);
        if (!g)
            return;
        if (idx < 0)
            idx = g->sets.size();
        // позиция среди наборов, которые остаются на месте
        int keepBefore = 0;
        for (int k = 0; k < idx && k < g->sets.size(); ++k)
            if (!sets.contains(Sel::set(tg, g->sets[k].id)))
                ++keepBefore;
        QVector<Sel> after;
        bool sameGraph = true;
        for (const Sel &s : sets)
            sameGraph &= s.gid == tg;
        m_doc->modify(T("Порядок наборов"), [&](Project &pr) {
            if (sameGraph) {
                Graph *gg = pr.graph(tg);
                QVector<int> keep, moving;
                for (const DataSet &d : gg->sets)
                    (sets.contains(Sel::set(tg, d.id)) ? moving : keep) << d.id;
                QVector<int> order = keep;
                for (int k = 0; k < moving.size(); ++k)
                    order.insert(keepBefore + k, moving[k]);
                const auto map = reorderSets(*gg, order);
                for (int sid : moving)
                    after << Sel::set(tg, map.value(sid, sid));
            } else {
                QVector<QPair<int, int>> pairs;
                for (const Sel &s : sets)
                    pairs << qMakePair(s.gid, s.sid);
                const auto map = moveSetsToGraph(pr, pairs, tg, keepBefore);
                for (const auto &p : pairs)
                    after << Sel::set(tg, map.value(p, p.second));
            }
        });
        m_doc->selectSets(after);
        return;
    }
    if (other.kind == Sel::Graph && t.kind == Sel::Graph && other.gid != t.gid) {
        int ngid = other.gid;
        m_doc->modify(T("Порядок графиков"), [&](Project &pr) {
            QVector<int> order;
            for (const Graph &g : pr.graphs)
                if (g.id != other.gid)
                    order << g.id;
            int pos = order.indexOf(t.gid) + (below ? 1 : 0);
            order.insert(qBound(0, pos, int(order.size())), other.gid);
            ngid = reorderGraphs(pr, order).value(other.gid, other.gid);
        });
        m_doc->select(Sel::graph(ngid));
        return;
    }
    if (other.kind == Sel::Object && t.kind == Sel::Object && other.obj != t.obj) {
        int ni = other.obj;
        m_doc->modify(T("Порядок надписей"), [&](Project &pr) {
            QVector<DrawObject> objs = pr.objects;
            const DrawObject moved = objs.takeAt(other.obj);
            int pos = t.obj - (other.obj < t.obj ? 1 : 0) + (below ? 1 : 0);
            pos = qBound(0, pos, int(objs.size()));
            objs.insert(pos, moved);
            pr.objects = objs;
            ni = pos;
        });
        m_doc->select(Sel::object(ni));
    }
}

void ProjectTree::syncSelection()
{
    m_updating = true;
    const Sel &s = m_doc->selection();
    Sel want = s;
    // легенда/заголовок/ось — подсвечиваем их график
    if (s.kind == Sel::Legend || s.kind == Sel::Title || s.kind == Sel::Axis)
        want = Sel::graph(s.gid);
    const QVector<Sel> sets = m_doc->selectedSets();
    QTreeWidgetItemIterator it(this);
    QTreeWidgetItem *found = nullptr;
    QList<QTreeWidgetItem *> extra;
    while (*it) {
        Sel cs = selFor(*it);
        if ((*it)->flags() & Qt::ItemIsSelectable) {
            if (cs == want && !found)
                found = *it;
            else if (cs.kind == Sel::Set && sets.contains(cs))
                extra << *it;
        }
        ++it;
    }
    clearSelection();
    for (auto *e : extra)
        e->setSelected(true);
    if (found) {
        found->setSelected(true);
        setCurrentItem(found, 0, QItemSelectionModel::NoUpdate);
        scrollToItem(found);
    }
    m_updating = false;
}

}  // namespace ng
