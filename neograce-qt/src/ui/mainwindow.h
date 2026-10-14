// Главное окно NeoGrace.
#pragma once

#include "document.h"
#include "setmath.h"

#include <QMainWindow>

class QAction;
class QActionGroup;
class QLabel;
class QSlider;
class QDockWidget;
class QMenu;

namespace ng {

class PageView;
class ProjectTree;
class Inspector;
class DataTable;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    bool openFile(const QString &path);
    static MainWindow *newWindow();
    void shotHook();  // для автоматических снимков экрана

protected:
    void closeEvent(QCloseEvent *) override;
    void dragEnterEvent(QDragEnterEvent *) override;
    void dropEvent(QDropEvent *) override;
    void changeEvent(QEvent *) override;

private:
    QAction *act(const QString &text, const QString &iconName, const QKeySequence &key,
                 const std::function<void()> &fn, const QString &tip = QString());
    void buildActions();
    void buildMenus();
    void buildToolbar();
    void updateTitle();
    void updateActions();
    void addRecent(const QString &path);
    void rebuildRecentMenu();
    bool maybeSave();

    // команды
    void fileNew();
    void fileOpen();
    bool fileSave();
    bool fileSaveAs();
    void fileImport(const QString &path = QString());
    void fileExport();
    void filePrint();
    void copyImage();
    void deleteSelection();
    void duplicateSelection();
    void addGraph();
    void arrange();
    void autoscale(const QString &axes = "xy", int gid = -1);
    void editText(const Sel &s);
    void showProperties(const Sel &s);
    void selectAllSets();
    void moveLayer(int move);
    void textData(const QString &text = QString(), const Sel &replace = Sel());
    void pasteData();
    void applyStyleNamed(const QString &key);
    void styleGallery();
    void addGradientActions(QMenu *menu);
    void setMath(const QString &op = QString());   // диалог «Операции над наборами»
    void quickSetMath(const SetMathSpec &spec);    // сразу, без диалога
    void showContextMenu(const Sel &s, const QPoint &pos);
    void showData(int gid = -1, int sid = -1);
    void analysis(int tab);
    void editTicks(int gid, int axis);
    void moveSetTo(int gid, int sid, int target);
    void about();

    Document *m_doc;
    PageView *m_view;
    ProjectTree *m_tree;
    Inspector *m_inspector;
    DataTable *m_data;
    QDockWidget *m_treeDock, *m_inspDock, *m_dataDock;
    QLabel *m_coords, *m_zoomLabel;
    QMenu *m_recentMenu = nullptr;
    QActionGroup *m_modeGroup = nullptr;
    QHash<QString, QAction *> a;
};

}  // namespace ng
