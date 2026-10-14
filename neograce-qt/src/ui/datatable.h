// Таблица данных выбранного набора: правка, вставка, копирование.
#pragma once

#include "document.h"

#include <QAbstractTableModel>
#include <QWidget>

class QTableView;
class QLabel;
class QComboBox;

namespace ng {

class SetModel : public QAbstractTableModel {
    Q_OBJECT
public:
    SetModel(Document *doc, QObject *parent = nullptr);
    void setTarget(int gid, int sid);
    int gid() const { return m_gid; }
    int sid() const { return m_sid; }
    const DataSet *set() const;

    int rowCount(const QModelIndex &) const override;
    int columnCount(const QModelIndex &) const override;
    QVariant data(const QModelIndex &, int role) const override;
    QVariant headerData(int section, Qt::Orientation, int role) const override;
    bool setData(const QModelIndex &, const QVariant &, int role) override;
    Qt::ItemFlags flags(const QModelIndex &) const override;
    void reload();

private:
    Document *m_doc;
    int m_gid = -1, m_sid = -1;
    int m_rows = 0, m_cols = 0;
    bool m_strings = false;
};

class DataTable : public QWidget {
    Q_OBJECT
public:
    explicit DataTable(Document *doc, QWidget *parent = nullptr);
    void showSet(int gid, int sid);

protected:
    bool eventFilter(QObject *, QEvent *) override;

private:
    void copySelection();
    void paste();
    void deleteRows();
    void insertRow();
    void updateTitle();

    Document *m_doc;
    SetModel *m_model;
    QTableView *m_view;
    QLabel *m_title;
    bool m_follow = true;
};

}  // namespace ng
