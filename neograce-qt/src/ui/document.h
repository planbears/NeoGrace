// Документ: проект, история отмен и текущее выделение.
#pragma once

#include "model.h"

#include <QElapsedTimer>
#include <QObject>
#include <QUndoCommand>
#include <QUndoStack>
#include <functional>

namespace ng {

struct Sel {
    enum Kind { None, Page, Graph, Set, Axis, Legend, Title, Object };
    Kind kind = None;
    int gid = -1, sid = -1, obj = -1, axis = -1;
    bool operator==(const Sel &o) const
    {
        return kind == o.kind && gid == o.gid && sid == o.sid && obj == o.obj && axis == o.axis;
    }
    bool operator!=(const Sel &o) const { return !(*this == o); }
    static Sel page() { Sel s; s.kind = Page; return s; }
    static Sel graph(int g) { Sel s; s.kind = Graph; s.gid = g; return s; }
    static Sel set(int g, int id) { Sel s; s.kind = Set; s.gid = g; s.sid = id; return s; }
    static Sel axisOf(int g, int a) { Sel s; s.kind = Axis; s.gid = g; s.axis = a; return s; }
    static Sel legend(int g) { Sel s; s.kind = Legend; s.gid = g; return s; }
    static Sel title(int g) { Sel s; s.kind = Title; s.gid = g; return s; }
    static Sel object(int i) { Sel s; s.kind = Object; s.obj = i; return s; }
};

// Какой набор свойств редактируется
struct PropTarget {
    enum Kind { GraphHeader, GraphP, AxisP, SetP, ObjectP };
    Kind kind = GraphP;
    int gid = -1, sid = -1, axis = 0, obj = -1;
};
Props *resolveProps(Project &pr, const PropTarget &t);

class Document : public QObject {
    Q_OBJECT
public:
    explicit Document(QObject *parent = nullptr);

    const Project &project() const { return m_pr; }
    QUndoStack *undo() { return &m_undo; }

    void setProject(const Project &pr);  // новый/открытый файл (без истории)
    // Изменение с записью в историю.  mergeKey — склеивать частые правки
    // одного и того же поля (перетаскивание, прокрутка чисел).
    void modify(const QString &text, const std::function<void(Project &)> &fn,
                const QString &mergeKey = QString());
    void setProp(const PropTarget &t, const QString &name, const QVariant &v, const QString &label = QString());
    // одно и то же свойство сразу у нескольких объектов (одна отмена)
    void setPropMany(const QVector<PropTarget> &ts, const QString &name, const QVariant &v,
                     const QString &label = QString());

    const Sel &selection() const { return m_sel; }
    void select(const Sel &s);
    // Групповое выделение наборов: основной — m_sel, все — selectedSets()
    QVector<Sel> selectedSets() const;
    bool isSetSelected(int gid, int sid) const;
    void toggleSet(const Sel &s);           // ⌘/⇧-щелчок
    void selectSets(const QVector<Sel> &sets, const Sel &primary = Sel());
    QVector<PropTarget> selectedSetTargets() const;
    int currentGraph() const;  // график выделения или первый видимый
    void setFilename(const QString &f) { m_pr.filename = f; }
    bool isModified() const { return !m_undo.isClean(); }

    // для команд отмены
    void restore(const Project &pr);

signals:
    void changed();
    void selectionChanged();

private:
    Project m_pr;
    QUndoStack m_undo;
    Sel m_sel;
    QVector<Sel> m_multi;  // дополнительные выделенные наборы (без основного)
    QElapsedTimer m_lastEdit;
    QString m_lastKey;
    friend class SnapshotCommand;
};

class SnapshotCommand : public QUndoCommand {
public:
    SnapshotCommand(Document *doc, const Project &before, const Project &after, const QString &text, int mergeId);
    void undo() override;
    void redo() override;
    int id() const override { return m_mergeId; }
    bool mergeWith(const QUndoCommand *other) override;

private:
    Document *m_doc;
    Project m_before, m_after;
    int m_mergeId;
    qint64 m_time = 0;
};

}  // namespace ng
