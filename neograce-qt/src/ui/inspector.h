// Инспектор свойств: формы для страницы, графика, оси, набора, объекта.
#pragma once

#include "document.h"

#include <QHash>
#include <QScrollArea>
#include <functional>

class QVBoxLayout;
class QLabel;

namespace ng {

class Section;

class Inspector : public QScrollArea {
    Q_OBJECT
public:
    explicit Inspector(Document *doc, QWidget *parent = nullptr);

    struct Field;
    // показать и поставить курсор в поле (например, "title", "label", "text")
    void focusProperty(const QString &name);

signals:
    void autoscaleWanted(int gid);
    void arrangeWanted();
    void styleGalleryWanted();
    void editTicksWanted(int gid, int axis);

private:
    void rebuild();
    void refresh();

    // построители
    Section *section(const QString &title, bool expanded = true);
    void addFields(Section *sec, const PropTarget &t, const QList<Field> &fields);
    void buildPage();
    void buildGraph(int gid, bool legendOnly = false, bool titleOnly = false);
    void buildAxis(int gid, int axis);
    void buildSet(int gid, int sid);
    void buildObject(int idx);
    void header(const QString &title, const QString &subtitle, const QString &iconName);

    Document *m_doc;
    QWidget *m_host = nullptr;
    QVBoxLayout *m_lay = nullptr;
    QList<std::function<void()>> m_refreshers;
    QHash<QString, QWidget *> m_editors;
    Sel m_built;
    bool m_refreshing = false;
};

}  // namespace ng
