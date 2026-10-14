#include "document.h"

#include "i18n.h"

#include <QDateTime>
#include <algorithm>

namespace ng {

Props *resolveProps(Project &pr, const PropTarget &t)
{
    if (t.kind == PropTarget::ObjectP) {
        if (t.obj < 0 || t.obj >= pr.objects.size())
            return nullptr;
        return &pr.objects[t.obj].p;
    }
    Graph *g = pr.graph(t.gid);
    if (!g)
        return nullptr;
    switch (t.kind) {
    case PropTarget::GraphHeader: return &g->hp;
    case PropTarget::GraphP: return &g->p;
    case PropTarget::AxisP: return (t.axis >= 0 && t.axis < 4) ? &g->axes[t.axis].p : nullptr;
    case PropTarget::SetP: {
        DataSet *s = g->set(t.sid);
        return s ? &s->p : nullptr;
    }
    default: return nullptr;
    }
}

Document::Document(QObject *parent) : QObject(parent)
{
    m_lastEdit.start();
    m_undo.setUndoLimit(300);
}

void Document::setProject(const Project &pr)
{
    m_pr = pr;
    m_undo.clear();
    m_undo.setClean();
    m_sel = Sel();
    emit changed();
    emit selectionChanged();
}

void Document::restore(const Project &pr)
{
    m_pr = pr;
    // выделение могло потерять смысл
    bool ok = true;
    switch (m_sel.kind) {
    case Sel::Graph: case Sel::Axis: case Sel::Legend: case Sel::Title:
        ok = m_pr.graph(m_sel.gid) != nullptr;
        break;
    case Sel::Set:
        ok = m_pr.graph(m_sel.gid) && m_pr.graph(m_sel.gid)->set(m_sel.sid);
        break;
    case Sel::Object:
        ok = m_sel.obj >= 0 && m_sel.obj < m_pr.objects.size();
        break;
    default: break;
    }
    const int before = m_multi.size();
    m_multi.erase(std::remove_if(m_multi.begin(), m_multi.end(), [this](const Sel &x) {
                      return !(m_pr.graph(x.gid) && m_pr.graph(x.gid)->set(x.sid));
                  }),
                  m_multi.end());
    emit changed();
    if (!ok) {
        m_sel = m_multi.isEmpty() ? Sel() : m_multi.takeLast();
        emit selectionChanged();
    } else if (before != m_multi.size()) {
        emit selectionChanged();
    }
}

void Document::modify(const QString &text, const std::function<void(Project &)> &fn, const QString &mergeKey)
{
    Project before = m_pr;
    Project after = m_pr;
    fn(after);
    int id = -1;
    if (!mergeKey.isEmpty())
        id = int(qHash(mergeKey) & 0x3fffffff) + 1;
    auto *cmd = new SnapshotCommand(this, before, after, text, id);
    m_undo.push(cmd);  // redo() применяет after и шлёт changed()
}

static void applyProp(Project &pr, const PropTarget &t, const QString &name, const QVariant &v)
{
    if (t.kind == PropTarget::SetP && name == "type") {  // тип набора меняет число столбцов
        if (Graph *g = pr.graph(t.gid))
            if (DataSet *s = g->set(t.sid))
                s->setType(v.toString());
        return;
    }
    if (Props *p = resolveProps(pr, t))
        p->set(name, v);
}

void Document::setProp(const PropTarget &t, const QString &name, const QVariant &v, const QString &label)
{
    Props *cur = resolveProps(m_pr, t);
    if (!cur || cur->get(name) == v)
        return;
    const QString key = QString("%1/%2/%3/%4/%5/%6").arg(int(t.kind)).arg(t.gid).arg(t.sid).arg(t.axis).arg(t.obj).arg(name);
    modify(label.isEmpty() ? T("Изменение свойства") : label, [&](Project &pr) { applyProp(pr, t, name, v); }, key);
}

void Document::setPropMany(const QVector<PropTarget> &ts, const QString &name, const QVariant &v, const QString &label)
{
    if (ts.size() == 1) {
        setProp(ts.first(), name, v, label);
        return;
    }
    bool any = false;
    for (const auto &t : ts)
        if (Props *p = resolveProps(m_pr, t))
            any |= p->get(name) != v;
    if (!any)
        return;
    QString key = "many/" + name;
    for (const auto &t : ts)
        key += QString("/%1.%2.%3").arg(int(t.kind)).arg(t.gid).arg(t.sid);
    modify(label.isEmpty() ? T("Изменение свойства") : label,
           [&](Project &pr) {
               for (const auto &t : ts)
                   applyProp(pr, t, name, v);
           },
           key);
}

QVector<Sel> Document::selectedSets() const
{
    QVector<Sel> out = m_multi;
    if (m_sel.kind == Sel::Set)
        out.append(m_sel);
    return out;
}

bool Document::isSetSelected(int gid, int sid) const
{
    for (const Sel &s : selectedSets())
        if (s.gid == gid && s.sid == sid)
            return true;
    return false;
}

QVector<PropTarget> Document::selectedSetTargets() const
{
    QVector<PropTarget> out;
    for (const Sel &s : selectedSets())
        out.append(PropTarget{PropTarget::SetP, s.gid, s.sid});
    return out;
}

void Document::toggleSet(const Sel &s)
{
    if (s.kind != Sel::Set)
        return;
    QVector<Sel> all = selectedSets();
    if (all.contains(s)) {
        all.removeAll(s);
        m_multi = all;
        m_sel = m_multi.isEmpty() ? Sel() : m_multi.takeLast();
    } else {
        m_multi = all;
        m_sel = s;
    }
    emit selectionChanged();
}

void Document::selectSets(const QVector<Sel> &sets, const Sel &primary)
{
    QVector<Sel> all;
    for (const Sel &s : sets)
        if (s.kind == Sel::Set && !all.contains(s))
            all.append(s);
    if (all.isEmpty()) {
        select(Sel());
        return;
    }
    Sel p = (primary.kind == Sel::Set && all.contains(primary)) ? primary : all.last();
    all.removeAll(p);
    if (p == m_sel && all == m_multi)
        return;
    m_sel = p;
    m_multi = all;
    emit selectionChanged();
}

void Document::select(const Sel &s)
{
    if (s == m_sel && m_multi.isEmpty())
        return;
    m_sel = s;
    m_multi.clear();
    emit selectionChanged();
}

int Document::currentGraph() const
{
    if (m_sel.gid >= 0 && m_pr.graph(m_sel.gid))
        return m_sel.gid;
    for (const auto &g : m_pr.graphs)
        if (g.visible())
            return g.id;
    return m_pr.graphs.isEmpty() ? -1 : m_pr.graphs.first().id;
}

// ---------------------------------------------------------------------------

static qint64 nowMs() { return QDateTime::currentMSecsSinceEpoch(); }

SnapshotCommand::SnapshotCommand(Document *doc, const Project &before, const Project &after, const QString &text,
                                 int mergeId)
    : QUndoCommand(text), m_doc(doc), m_before(before), m_after(after), m_mergeId(mergeId)
{
    m_time = nowMs();
}

void SnapshotCommand::undo() { m_doc->restore(m_before); }

void SnapshotCommand::redo() { m_doc->restore(m_after); }

bool SnapshotCommand::mergeWith(const QUndoCommand *other)
{
    auto *o = dynamic_cast<const SnapshotCommand *>(other);
    if (!o || o->m_mergeId != m_mergeId || m_mergeId < 0)
        return false;
    if (o->m_time - m_time > 1500)
        return false;
    m_after = o->m_after;
    m_time = o->m_time;
    return true;
}

}  // namespace ng
