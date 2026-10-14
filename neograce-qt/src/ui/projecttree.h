// Боковая панель: структура проекта (графики, наборы, объекты).
#pragma once

#include "document.h"

#include <QTreeWidget>

namespace ng {

class ProjectTree : public QTreeWidget {
    Q_OBJECT
public:
    explicit ProjectTree(Document *doc, QWidget *parent = nullptr);

signals:
    void contextMenuWanted(const ng::Sel &sel, const QPoint &globalPos);
    void openDataWanted(int gid, int sid);
    void propertiesWanted(const ng::Sel &sel);

protected:
    void dropEvent(QDropEvent *e) override;

private:
    void rebuild();
    void syncSelection();
    Sel selFor(QTreeWidgetItem *it) const;

    Document *m_doc;
    bool m_updating = false;
};

}  // namespace ng
