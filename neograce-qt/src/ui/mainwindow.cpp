#include "mainwindow.h"

#include "agrio.h"
#include "datatable.h"
#include "dialogs.h"
#include "export.h"
#include "gracetext.h"
#include "i18n.h"
#include "icons.h"
#include "inspector.h"
#include "ops.h"
#include "pageview.h"
#include "projecttree.h"

#include <algorithm>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QTextStream>
#include <QFileInfo>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPrintDialog>
#include <QPrinter>
#include <QSettings>
#include <QSlider>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUndoStack>
#include <cmath>

namespace ng {

static const char *kFileFilter =
    "Все поддерживаемые (*.agr *.xmgr *.par *.xvg *.dat *.txt *.csv *.tsv);;"
    "Проекты Grace (*.agr *.xmgr *.par);;Данные GROMACS (*.xvg);;Таблицы (*.dat *.txt *.csv *.tsv);;Все файлы (*)";

MainWindow *MainWindow::newWindow()
{
    auto *w = new MainWindow();
    w->setAttribute(Qt::WA_DeleteOnClose);
    w->show();
    return w;
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    m_doc = new Document(this);
    setUnifiedTitleAndToolBarOnMac(true);
    setAcceptDrops(true);
    setDocumentMode(true);
    resize(1320, 860);

    m_view = new PageView(m_doc, this);
    setCentralWidget(m_view);

    m_tree = new ProjectTree(m_doc, this);
    m_treeDock = new QDockWidget(T("Проект"), this);
    m_treeDock->setObjectName("tree");
    m_treeDock->setWidget(m_tree);
    m_treeDock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable);
    addDockWidget(Qt::LeftDockWidgetArea, m_treeDock);

    m_inspector = new Inspector(m_doc, this);
    m_inspDock = new QDockWidget(T("Свойства"), this);
    m_inspDock->setObjectName("inspector");
    m_inspDock->setWidget(m_inspector);
    m_inspDock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable);
    addDockWidget(Qt::RightDockWidgetArea, m_inspDock);

    m_data = new DataTable(m_doc, this);
    m_dataDock = new QDockWidget(T("Данные"), this);
    m_dataDock->setObjectName("data");
    m_dataDock->setWidget(m_data);
    m_dataDock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    addDockWidget(Qt::BottomDockWidgetArea, m_dataDock);
    m_dataDock->hide();
    resizeDocks({m_treeDock, m_inspDock}, {240, 330}, Qt::Horizontal);

    m_coords = new QLabel(this);
    m_coords->setMinimumWidth(320);
    statusBar()->addWidget(m_coords, 1);
    m_zoomLabel = new QLabel(this);
    statusBar()->addPermanentWidget(m_zoomLabel);
    statusBar()->setSizeGripEnabled(false);

    buildActions();
    buildMenus();
    buildToolbar();

    connect(m_view, &PageView::cursorInfo, m_coords, &QLabel::setText);
    connect(m_view, &PageView::zoomChanged, this, [this](double z) {
        m_zoomLabel->setText(QStringLiteral("%1%").arg(qRound(z / (logicalDpiX() / 72.0 * 0.75) * 100)));
    });
    connect(m_view, &PageView::contextMenuWanted, this, &MainWindow::showContextMenu);
    connect(m_tree, &ProjectTree::contextMenuWanted, this, &MainWindow::showContextMenu);
    connect(m_view, &PageView::editTextWanted, this, &MainWindow::editText);
    connect(m_view, &PageView::openDataWanted, this, &MainWindow::showData);
    connect(m_tree, &ProjectTree::openDataWanted, this, &MainWindow::showData);
    connect(m_view, &PageView::propertiesWanted, this, &MainWindow::showProperties);
    connect(m_tree, &ProjectTree::propertiesWanted, this, &MainWindow::showProperties);
    connect(m_view, &PageView::modeChanged, this, [this](int m) {
        for (QAction *x : m_modeGroup->actions())
            if (x->data().toInt() == m)
                x->setChecked(true);
    });
    connect(m_inspector, &Inspector::autoscaleWanted, this, [this](int gid) { autoscale("xy", gid); });
    connect(m_inspector, &Inspector::arrangeWanted, this, &MainWindow::arrange);
    connect(m_inspector, &Inspector::styleGalleryWanted, this, &MainWindow::styleGallery);
    connect(m_inspector, &Inspector::editTicksWanted, this, &MainWindow::editTicks);
    connect(m_doc->undo(), &QUndoStack::cleanChanged, this, &MainWindow::updateTitle);
    connect(m_doc, &Document::changed, this, &MainWindow::updateTitle);
    connect(m_doc, &Document::selectionChanged, this, &MainWindow::updateActions);

    QSettings st;
    restoreState(st.value("window/state").toByteArray());
    restoreGeometry(st.value("window/geometry").toByteArray());
    m_dataDock->hide();

    m_doc->setProject(newProject(true));
    updateTitle();
    updateActions();
}

QAction *MainWindow::act(const QString &text, const QString &iconName, const QKeySequence &key,
                         const std::function<void()> &fn, const QString &tip)
{
    auto *x = new QAction(text, this);
    if (!iconName.isEmpty())
        x->setIcon(icon(iconName));
    if (!key.isEmpty())
        x->setShortcut(key);
    x->setToolTip(tip.isEmpty() ? (key.isEmpty() ? text : QString("%1  (%2)").arg(text, key.toString(QKeySequence::NativeText)))
                                : tip);
    connect(x, &QAction::triggered, this, [fn] { fn(); });
    return x;
}

void MainWindow::buildActions()
{
    a["new"] = act(T("Новый"), "new", QKeySequence::New, [this] { fileNew(); });
    a["open"] = act(T("Открыть…"), "open", QKeySequence::Open, [this] { fileOpen(); });
    a["save"] = act(T("Сохранить"), "save", QKeySequence::Save, [this] { fileSave(); });
    a["saveas"] = act(T("Сохранить как…"), "", QKeySequence::SaveAs, [this] { fileSaveAs(); });
    a["import"] = act(T("Импорт данных…"), "import", QKeySequence(Qt::CTRL | Qt::Key_I), [this] { fileImport(); });
    a["export"] = act(T("Экспорт изображения…"), "export", QKeySequence(Qt::CTRL | Qt::Key_E), [this] { fileExport(); });
    a["print"] = act(T("Печать…"), "print", QKeySequence::Print, [this] { filePrint(); });
    a["close"] = act(T("Закрыть окно"), "", QKeySequence::Close, [this] { close(); });
    a["quit"] = act(T("Выйти"), "", QKeySequence::Quit, [] { qApp->closeAllWindows(); });
    a["quit"]->setMenuRole(QAction::QuitRole);

    a["undo"] = m_doc->undo()->createUndoAction(this, T("Отменить"));
    a["undo"]->setIcon(icon("undo"));
    a["undo"]->setShortcut(QKeySequence::Undo);
    a["redo"] = m_doc->undo()->createRedoAction(this, T("Повторить"));
    a["redo"]->setIcon(icon("redo"));
    a["redo"]->setShortcut(QKeySequence::Redo);
    a["copyimg"] = act(T("Копировать как картинку"), "copy", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C), [this] { copyImage(); });
    a["dup"] = act(T("Дублировать"), "copy", QKeySequence(Qt::CTRL | Qt::Key_D), [this] { duplicateSelection(); });
    a["delete"] = act(T("Удалить"), "trash", QKeySequence::Delete, [this] { deleteSelection(); });
    a["front"] = act(T("На передний план"), "", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_BracketRight), [this] { moveLayer(LayerFront); });
    a["forward"] = act(T("Вперёд"), "", QKeySequence(Qt::CTRL | Qt::Key_BracketRight), [this] { moveLayer(LayerForward); });
    a["backward"] = act(T("Назад"), "", QKeySequence(Qt::CTRL | Qt::Key_BracketLeft), [this] { moveLayer(LayerBackward); });
    a["back"] = act(T("На задний план"), "", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_BracketLeft), [this] { moveLayer(LayerBack); });
    a["selectall"] = act(T("Выделить все наборы графика"), "", QKeySequence::SelectAll, [this] { selectAllSets(); });
    a["selectall"]->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    m_view->addAction(a["selectall"]);
    m_tree->addAction(a["selectall"]);
    a["delete"]->setShortcuts({QKeySequence::Delete, QKeySequence(Qt::Key_Backspace)});
    a["delete"]->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    m_view->addAction(a["delete"]);
    m_tree->addAction(a["delete"]);

    a["zoomin"] = act(T("Крупнее"), "plus", QKeySequence::ZoomIn, [this] { m_view->zoomIn(); });
    a["zoomout"] = act(T("Мельче"), "minus", QKeySequence::ZoomOut, [this] { m_view->zoomOut(); });
    a["zoomfit"] = act(T("По размеру окна"), "fit", QKeySequence(Qt::CTRL | Qt::Key_0), [this] { m_view->zoomFit(); });
    a["100"] = act(T("Реальный размер"), "", QKeySequence(Qt::CTRL | Qt::Key_1), [this] { m_view->zoom100(); });
    a["sidebar"] = m_treeDock->toggleViewAction();
    a["sidebar"]->setIcon(icon("sidebar"));
    a["sidebar"]->setText(T("Панель проекта"));
    a["sidebar"]->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_1));
    a["insp"] = m_inspDock->toggleViewAction();
    a["insp"]->setIcon(icon("inspector"));
    a["insp"]->setText(T("Свойства"));
    a["insp"]->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_2));
    a["data"] = act(T("Таблица данных"), "table", QKeySequence(Qt::CTRL | Qt::Key_T), [this] {
        if (m_dataDock->isVisible())
            m_dataDock->hide();
        else
            showData();
    });

    m_modeGroup = new QActionGroup(this);
    struct M { PageView::Mode m; const char *text; const char *icon; Qt::Key key; };
    static const M modes[] = {{PageView::Select, "Выделение", "select", Qt::Key_V},
                              {PageView::Zoom, "Увеличение рамкой", "zoom", Qt::Key_Z},
                              {PageView::Pan, "Сдвиг графика", "pan", Qt::Key_H},
                              {PageView::AddText, "Текст", "text", Qt::Key_T},
                              {PageView::AddLine, "Линия со стрелкой", "line", Qt::Key_L},
                              {PageView::AddBox, "Прямоугольник", "box", Qt::Key_R},
                              {PageView::AddEllipse, "Эллипс", "ellipse", Qt::Key_O}};
    for (const auto &m : modes) {
        const PageView::Mode mode = m.m;
        QAction *x = act(T(m.text), m.icon, QKeySequence(m.key), [this, mode] { m_view->setMode(mode); });
        x->setCheckable(true);
        x->setData(int(mode));
        x->setShortcutContext(Qt::WidgetShortcut);  // одиночные клавиши — только на холсте
        m_view->addAction(x);
        m_modeGroup->addAction(x);
        a[QString("mode%1").arg(int(mode))] = x;
    }
    a["mode0"]->setChecked(true);

    a["addgraph"] = act(T("Добавить график"), "addgraph", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_G), [this] { addGraph(); });
    a["arrange"] = act(T("Расставить графики…"), "arrange", QKeySequence(), [this] { arrange(); });
    a["autoscale"] = act(T("Автомасштаб"), "autoscale", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_A), [this] { autoscale(); });
    a["autox"] = act(T("Автомасштаб по X"), "", QKeySequence(), [this] { autoscale("x"); });
    a["autoy"] = act(T("Автомасштаб по Y"), "", QKeySequence(), [this] { autoscale("y"); });
    a["fitmargins"] = act(T("Подогнать поля под подписи"), "", QKeySequence(), [this] {
        m_doc->modify(T("Поля"), [](Project &pr) { fitMargins(pr); });
    });
    for (const StyleDef &sd : styleDefs()) {
        const QString key = sd.key;
        a["style_" + key] = act(T(sd.name), "", QKeySequence(), [this, key] { applyStyleNamed(key); }, T(sd.tip));
    }
    a["stylegallery"] = act(T("Галерея стилей…"), "palette", QKeySequence(), [this] { styleGallery(); },
                            T("Посмотреть проект во всех стилях оформления и выбрать подходящий"));

    a["textdata"] = act(T("Набор из текста…"), "text", QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_N), [this] { textData(); },
                        T("Ввести точки набора числами — например, «20 -10» и «20 10» дают отрезок"));
    a["pastedata"] = act(T("Вставить данные как набор"), "paste", QKeySequence::Paste, [this] { pasteData(); });
    a["pastedata"]->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    m_view->addAction(a["pastedata"]);
    a["setmath"] = act(T("Операции над наборами…"), "setmath", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_M), [this] { setMath(); },
                       T("Сложить, вычесть, умножить или разделить наборы; умножить на число; среднее, нормировка…"));
    a["formula"] = act(T("Формула для набора…"), "function", QKeySequence(Qt::CTRL | Qt::Key_F), [this] { analysis(0); });
    a["create"] = act(T("Новый набор по формуле…"), "function", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N), [this] { analysis(1); });
    a["regression"] = act(T("Регрессия…"), "fit_curve", QKeySequence(), [this] { analysis(2); });
    a["fit"] = act(T("Нелинейная аппроксимация…"), "fit_curve", QKeySequence(Qt::CTRL | Qt::Key_K), [this] { analysis(3); });
    a["transform"] = act(T("Преобразования…"), "wand", QKeySequence(), [this] { analysis(4); });
    a["exportset"] = act(T("Сохранить набор как таблицу…"), "", QKeySequence(), [this] {
        const Sel s = m_doc->selection();
        const Graph *g = m_doc->project().graph(s.gid);
        const DataSet *d = g ? g->set(s.sid) : nullptr;
        if (!d) {
            QMessageBox::information(this, "NeoGrace", T("Сначала выберите набор."));
            return;
        }
        QString path = QFileDialog::getSaveFileName(this, T("Сохранить набор"),
                                                    QString("set_G%1_S%2.dat").arg(s.gid).arg(s.sid),
                                                    T("Таблица (*.dat *.txt *.csv)"));
        if (path.isEmpty())
            return;
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
            return;
        const bool csv = path.endsWith(".csv", Qt::CaseInsensitive);
        QTextStream ts(&f);
        ts << (csv ? "" : "# ") << setColumnNames(d->type()).join(csv ? "," : "\t") << "\n";
        for (int i = 0; i < d->size(); ++i) {
            QStringList row;
            for (const auto &c : d->cols)
                row << QString::number(c.value(i), 'g', 12);
            ts << row.join(csv ? "," : "\t") << "\n";
        }
    });
    a["about"] = act(T("О программе NeoGrace"), "", QKeySequence(), [this] { about(); });
    a["about"]->setMenuRole(QAction::AboutRole);
}

void MainWindow::buildMenus()
{
    QMenu *m = menuBar()->addMenu(T("Файл"));
    m->addActions({a["new"], a["open"]});
    m_recentMenu = m->addMenu(T("Недавние"));
    rebuildRecentMenu();
    m->addSeparator();
    m->addActions({a["save"], a["saveas"]});
    m->addSeparator();
    m->addActions({a["import"], a["export"], a["copyimg"], a["print"]});
    m->addSeparator();
    m->addActions({a["close"], a["quit"]});

    m = menuBar()->addMenu(T("Правка"));
    m->addActions({a["undo"], a["redo"]});
    m->addSeparator();
    m->addAction(a["pastedata"]);
    m->addActions({a["dup"], a["delete"]});
    m->addSeparator();
    m->addAction(a["selectall"]);
    m->addSeparator();
    QMenu *om = m->addMenu(T("Порядок слоёв"));
    om->addActions({a["front"], a["forward"], a["backward"], a["back"]});

    m = menuBar()->addMenu(T("Вид"));
    m->addActions({a["zoomin"], a["zoomout"], a["zoomfit"], a["100"]});
    m->addSeparator();
    m->addActions({a["sidebar"], a["insp"], a["data"]});
    m->addSeparator();
    for (QAction *x : m_modeGroup->actions())
        m->addAction(x);

    // язык интерфейса (подпись двуязычная, чтобы найти в любом режиме)
    QMenu *lm = m->addMenu(QString::fromUtf8("Язык / Language"));
    auto *lg = new QActionGroup(lm);
    const QString curLang = QSettings().value("language").toString();
    struct L { const char *key; const char *name; };
    static const L langs[] = {{"", "Как в системе / System"}, {"ru", "Русский"}, {"en", "English"}};
    for (const auto &l : langs) {
        QAction *x = lm->addAction(QString::fromUtf8(l.name));
        x->setCheckable(true);
        x->setChecked(curLang == QLatin1String(l.key));
        lg->addAction(x);
        const QString key = QString::fromLatin1(l.key);
        connect(x, &QAction::triggered, this, [this, key] {
            QSettings().setValue("language", key);
            QMessageBox::information(this, "NeoGrace",
                                     QString::fromUtf8("Язык изменится после перезапуска NeoGrace.\n"
                                                       "The language will change after restarting NeoGrace."));
        });
    }

    m = menuBar()->addMenu(T("График"));
    m->addActions({a["addgraph"], a["arrange"]});
    m->addSeparator();
    m->addActions({a["autoscale"], a["autox"], a["autoy"], a["fitmargins"]});
    m->addSeparator();
    m->addAction(a["stylegallery"]);
    {
        QMenu *sm = m->addMenu(T("Стиль оформления"));
        for (const StyleDef &sd : styleDefs())
            sm->addAction(a["style_" + sd.key]);
        QMenu *pm = m->addMenu(T("Палитра"));
        for (const PaletteDef &pd : paletteDefs()) {
            const QString key = pd.key;
            pm->addAction(T(pd.name), this, [this, key] {
                m_doc->modify(T("Палитра"), [key](Project &pr) {
                    if (const auto *c = paletteColors(key))
                        pr.applyPalette(*c);
                });
            });
        }
        pm->addSeparator();
        addGradientActions(pm);
    }

    m = menuBar()->addMenu(T("Данные"));
    m->addActions({a["import"], a["textdata"], a["create"], a["formula"], a["setmath"]});
    m->addSeparator();
    m->addActions({a["regression"], a["fit"], a["transform"]});
    m->addSeparator();
    m->addAction(a["exportset"]);

    m = menuBar()->addMenu(T("Справка"));
    m->addAction(a["about"]);
    m->addAction(T("Разметка текста Grace"), this, [this] {
        QMessageBox::information(this, T("Разметка текста"),
                                 T("<b>\\S</b>…<b>\\N</b> — верхний индекс: x\\S2\\N → x²<br>" "<b>\\s</b>…<b>\\N</b> — нижний индекс: H\\s2\\NO → H₂O<br>" "<b>\\x</b> — греческий шрифт: \\xa\\f{} → α, \\xD\\f{} → Δ<br>" "<b>\\f{}</b> — вернуть шрифт; <b>\\f{Helvetica}</b> или <b>\\4</b> — сменить<br>" "<b>\\+ \\-</b> — крупнее/мельче; <b>\\u…\\U</b> подчеркнуть; " "<b>\\o…\\O</b> черта сверху<br><b>\\R{red}</b> — цвет; <b>\\#{b0}</b> — символ по коду (°)<br><br>" "Греческие буквы, набранные с клавиатуры, превращаются в разметку автоматически."));
    });
}

void MainWindow::buildToolbar()
{
    QToolBar *tb = addToolBar(T("Панель инструментов"));
    tb->setObjectName("main");
    tb->setMovable(false);
    tb->setIconSize(QSize(20, 20));
    tb->setToolButtonStyle(Qt::ToolButtonIconOnly);
    tb->addActions({a["open"], a["save"], a["export"]});
    tb->addSeparator();
    tb->addActions({a["undo"], a["redo"]});
    tb->addSeparator();
    for (QAction *x : m_modeGroup->actions())
        tb->addAction(x);
    tb->addSeparator();
    tb->addActions({a["autoscale"], a["addgraph"], a["import"], a["textdata"], a["setmath"], a["data"], a["fit"],
                    a["stylegallery"]});
    auto *spacer = new QWidget(tb);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tb->addWidget(spacer);
    tb->addActions({a["zoomout"], a["zoomfit"], a["zoomin"]});
    tb->addSeparator();
    tb->addActions({a["sidebar"], a["insp"]});
}

void MainWindow::changeEvent(QEvent *e)
{
    if (e->type() == QEvent::PaletteChange || e->type() == QEvent::ThemeChange) {
        clearIconCache();
        static const QHash<QString, QString> ic = {
            {"new", "new"}, {"open", "open"}, {"save", "save"}, {"import", "import"}, {"export", "export"},
            {"print", "print"}, {"undo", "undo"}, {"redo", "redo"}, {"copyimg", "copy"}, {"dup", "copy"},
            {"delete", "trash"}, {"zoomin", "plus"}, {"zoomout", "minus"}, {"sidebar", "sidebar"},
            {"insp", "inspector"}, {"data", "table"}, {"addgraph", "addgraph"}, {"arrange", "arrange"},
            {"autoscale", "autoscale"}, {"formula", "function"}, {"textdata", "text"}, {"setmath", "setmath"}, {"stylegallery", "palette"}, {"pastedata", "paste"}, {"create", "function"}, {"regression", "fit_curve"},
            {"transform", "wand"}, {"mode0", "select"}, {"mode1", "zoom"}, {"mode2", "pan"}, {"mode3", "text"},
            {"mode4", "line"}, {"mode5", "box"}, {"mode6", "ellipse"}};
        for (auto it = ic.begin(); it != ic.end(); ++it)
            if (a.contains(it.key()))
                a[it.key()]->setIcon(icon(it.value()));
        a["fit"]->setIcon(icon("fit_curve"));
        a["zoomfit"]->setIcon(icon("fit"));
    }
    QMainWindow::changeEvent(e);
}

void MainWindow::updateTitle()
{
    const Project &pr = m_doc->project();
    const QString name = pr.filename.isEmpty() ? T("Без названия") : QFileInfo(pr.filename).fileName();
    setWindowTitle(name + "[*] — NeoGrace");
    setWindowFilePath(pr.filename);
    setWindowModified(m_doc->isModified());
}

void MainWindow::updateActions()
{
    const Sel &s = m_doc->selection();
    const bool del = s.kind == Sel::Set || s.kind == Sel::Graph || s.kind == Sel::Object;
    a["delete"]->setEnabled(del);
    a["dup"]->setEnabled(del);
}

// ---------------------------------------------------------------------------
// Файлы

bool MainWindow::maybeSave()
{
    if (!m_doc->isModified())
        return true;
    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setText(T("Сохранить изменения в «%1»?")
                    .arg(m_doc->project().filename.isEmpty() ? T("Без названия")
                                                             : QFileInfo(m_doc->project().filename).fileName()));
    box.setInformativeText(T("Если не сохранить, изменения пропадут."));
    box.setStandardButtons(QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    box.setDefaultButton(QMessageBox::Save);
    box.button(QMessageBox::Save)->setText(T("Сохранить"));
    box.button(QMessageBox::Discard)->setText(T("Не сохранять"));
    box.button(QMessageBox::Cancel)->setText(T("Отмена"));
    box.setWindowModality(Qt::WindowModal);
    const int r = box.exec();
    if (r == QMessageBox::Save)
        return fileSave();
    return r == QMessageBox::Discard;
}

void MainWindow::fileNew()
{
    newWindow();
}

void MainWindow::fileOpen()
{
    QSettings st;
    const QStringList paths = QFileDialog::getOpenFileNames(this, T("Открыть"), st.value("lastDir").toString(),
                                                            T(kFileFilter));
    for (const QString &p : paths) {
        st.setValue("lastDir", QFileInfo(p).absolutePath());
        // пустое окно используем, иначе — новое
        const bool empty = !m_doc->isModified() && m_doc->project().filename.isEmpty()
                           && std::all_of(m_doc->project().graphs.begin(), m_doc->project().graphs.end(),
                                          [](const Graph &g) { return g.sets.isEmpty(); });
        if (empty)
            openFile(p);
        else
            newWindow()->openFile(p);
    }
}

bool MainWindow::openFile(const QString &path)
{
    Project pr;
    QString err;
    if (!loadProject(path, pr, &err)) {
        QMessageBox::warning(this, "NeoGrace", T("Не удалось открыть «%1»:\n%2").arg(QFileInfo(path).fileName(), err));
        return false;
    }
    const bool isData = pr.filename.isEmpty();
    m_doc->setProject(pr);
    if (isData) {
        // файл данных: сохранять предложим как .agr рядом
        m_doc->setFilename(QString());
        setWindowTitle(QFileInfo(path).fileName() + "[*] — NeoGrace");
    }
    addRecent(path);
    updateTitle();
    if (isData)
        setWindowTitle(QFileInfo(path).completeBaseName() + ".agr[*] — NeoGrace");
    m_view->zoomFit();
    if (!pr.warnings.isEmpty())
        statusBar()->showMessage(T("Есть непонятные строки (%1), они пропущены: %2")
                                     .arg(pr.warnings.size()).arg(pr.warnings.first()), 8000);
    else
        statusBar()->showMessage(T("Открыт %1").arg(QFileInfo(path).fileName()), 4000);
    return true;
}

bool MainWindow::fileSave()
{
    if (m_doc->project().filename.isEmpty())
        return fileSaveAs();
    Project pr = m_doc->project();
    QString err;
    if (!saveProject(pr, pr.filename, &err)) {
        QMessageBox::warning(this, "NeoGrace", T("Не удалось сохранить:\n%1").arg(err));
        return false;
    }
    m_doc->undo()->setClean();
    statusBar()->showMessage(T("Сохранено: %1").arg(pr.filename), 3000);
    updateTitle();
    return true;
}

bool MainWindow::fileSaveAs()
{
    QSettings st;
    QString start = m_doc->project().filename;
    if (start.isEmpty())
        start = QDir(st.value("lastDir", QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).toString())
                    .filePath(T("График.agr"));
    QString path = QFileDialog::getSaveFileName(this, T("Сохранить как"), start, T("Проект Grace (*.agr)"));
    if (path.isEmpty())
        return false;
    if (QFileInfo(path).suffix().isEmpty())
        path += ".agr";
    Project pr = m_doc->project();
    QString err;
    if (!saveProject(pr, path, &err)) {
        QMessageBox::warning(this, "NeoGrace", T("Не удалось сохранить:\n%1").arg(err));
        return false;
    }
    m_doc->setFilename(path);
    m_doc->undo()->setClean();
    st.setValue("lastDir", QFileInfo(path).absolutePath());
    addRecent(path);
    updateTitle();
    return true;
}

void MainWindow::fileImport(const QString &path0)
{
    QString path = path0;
    QSettings st;
    if (path.isEmpty()) {
        path = QFileDialog::getOpenFileName(this, T("Импорт данных"), st.value("lastDir").toString(),
                                            T("Данные (*.dat *.txt *.csv *.tsv *.xvg *.xy);;Все файлы (*)"));
        if (path.isEmpty())
            return;
        st.setValue("lastDir", QFileInfo(path).absolutePath());
    }
    ImportDialog dlg(m_doc, path, this);
    if (!dlg.ok()) {
        QMessageBox::warning(this, "NeoGrace", T("В файле не найдено числовых данных."));
        return;
    }
    if (dlg.exec() != QDialog::Accepted)
        return;
    const int gid = dlg.apply();
    m_doc->select(Sel::graph(gid));
}

void MainWindow::fileExport()
{
    QSettings st;
    ExportDialog dlg(st.value("export/format", "pdf").toString(), this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    const QString fmt = dlg.format();
    st.setValue("export/format", fmt);
    QString base = m_doc->project().filename.isEmpty() ? T("График") : QFileInfo(m_doc->project().filename).completeBaseName();
    QString dir = st.value("export/dir", st.value("lastDir").toString()).toString();
    QString path = QFileDialog::getSaveFileName(this, T("Экспорт"), QDir(dir).filePath(base + "." + fmt),
                                                QString("%1 (*.%2)").arg(fmt.toUpper(), fmt));
    if (path.isEmpty())
        return;
    if (QFileInfo(path).suffix().isEmpty())
        path += "." + fmt;
    st.setValue("export/dir", QFileInfo(path).absolutePath());
    QString err;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool ok = exportFile(m_doc->project(), path, dlg.dpi(), dlg.transparent(), &err);
    QApplication::restoreOverrideCursor();
    if (!ok)
        QMessageBox::warning(this, "NeoGrace", T("Не удалось сохранить:\n%1").arg(err));
    else
        statusBar()->showMessage(T("Экспортировано: %1").arg(path), 4000);
}

void MainWindow::filePrint()
{
    QPrinter printer(QPrinter::HighResolution);
    const Project &pr = m_doc->project();
    printer.setPageOrientation(pr.pageW > pr.pageH ? QPageLayout::Landscape : QPageLayout::Portrait);
    QPrintDialog dlg(&printer, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    printProject(pr, &printer);
}

void MainWindow::copyImage()
{
    QImage img = renderImage(m_doc->project(), 200);
    auto *md = new QMimeData;
    md->setImageData(img);
    QApplication::clipboard()->setMimeData(md);
    statusBar()->showMessage(T("Картинка скопирована в буфер обмена"), 3000);
}

void MainWindow::addRecent(const QString &path)
{
    QSettings st;
    QStringList r = st.value("recent").toStringList();
    const QString abs = QFileInfo(path).absoluteFilePath();
    r.removeAll(abs);
    r.prepend(abs);
    while (r.size() > 12)
        r.removeLast();
    st.setValue("recent", r);
    rebuildRecentMenu();
}

void MainWindow::rebuildRecentMenu()
{
    if (!m_recentMenu)
        return;
    m_recentMenu->clear();
    const QStringList r = QSettings().value("recent").toStringList();
    for (const QString &p : r) {
        if (!QFileInfo::exists(p))
            continue;
        m_recentMenu->addAction(QFileInfo(p).fileName(), this, [this, p] {
            const bool empty = !m_doc->isModified() && m_doc->project().filename.isEmpty()
                               && std::all_of(m_doc->project().graphs.begin(), m_doc->project().graphs.end(),
                                              [](const Graph &g) { return g.sets.isEmpty(); });
            if (empty)
                openFile(p);
            else
                newWindow()->openFile(p);
        })->setToolTip(p);
    }
    if (m_recentMenu->isEmpty())
        m_recentMenu->addAction(T("Пусто"))->setEnabled(false);
    else {
        m_recentMenu->addSeparator();
        m_recentMenu->addAction(T("Очистить"), this, [this] {
            QSettings().setValue("recent", QStringList());
            rebuildRecentMenu();
        });
    }
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    if (!maybeSave()) {
        e->ignore();
        return;
    }
    QSettings st;
    st.setValue("window/state", saveState());
    st.setValue("window/geometry", saveGeometry());
    e->accept();
}

void MainWindow::dragEnterEvent(QDragEnterEvent *e)
{
    if (e->mimeData()->hasUrls())
        e->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *e)
{
    for (const QUrl &u : e->mimeData()->urls()) {
        const QString p = u.toLocalFile();
        if (p.isEmpty())
            continue;
        const QString ext = QFileInfo(p).suffix().toLower();
        if (ext == "agr" || ext == "par" || ext == "xmgr") {
            const bool empty = !m_doc->isModified() && m_doc->project().filename.isEmpty()
                               && std::all_of(m_doc->project().graphs.begin(), m_doc->project().graphs.end(),
                                              [](const Graph &g) { return g.sets.isEmpty(); });
            if (empty)
                openFile(p);
            else
                newWindow()->openFile(p);
        } else {
            fileImport(p);  // данные — в текущий проект
        }
    }
}

// ---------------------------------------------------------------------------
// Правка

void MainWindow::deleteSelection()
{
    const Sel s = m_doc->selection();
    if (s.kind == Sel::Set) {
        const QVector<Sel> sets = m_doc->selectedSets();
        m_doc->modify(sets.size() > 1 ? T("Удаление наборов") : T("Удаление набора"), [&](Project &pr) {
            for (const Sel &x : sets)
                if (Graph *g = pr.graph(x.gid))
                    g->sets.erase(std::remove_if(g->sets.begin(), g->sets.end(), [&](const DataSet &d) { return d.id == x.sid; }),
                                  g->sets.end());
        });
        m_doc->select(Sel::graph(s.gid));
    } else if (s.kind == Sel::Graph) {
        if (m_doc->project().graphs.size() <= 1) {
            statusBar()->showMessage(T("Последний график удалить нельзя"), 3000);
            return;
        }
        m_doc->modify(T("Удаление графика"), [&](Project &pr) {
            pr.graphs.erase(std::remove_if(pr.graphs.begin(), pr.graphs.end(), [&](const Graph &g) { return g.id == s.gid; }),
                            pr.graphs.end());
            pr.objects.erase(std::remove_if(pr.objects.begin(), pr.objects.end(),
                                            [&](const DrawObject &o) { return o.p.s("loctype") == "world" && o.graph == s.gid; }),
                             pr.objects.end());
        });
        m_doc->select(Sel());
    } else if (s.kind == Sel::Object) {
        m_doc->modify(T("Удаление объекта"), [&](Project &pr) {
            if (s.obj >= 0 && s.obj < pr.objects.size())
                pr.objects.removeAt(s.obj);
        });
        m_doc->select(Sel());
    }
}

void MainWindow::duplicateSelection()
{
    const Sel s = m_doc->selection();
    if (s.kind == Sel::Set) {
        const QVector<Sel> sets = m_doc->selectedSets();
        QVector<Sel> made;
        m_doc->modify(T("Копия набора"), [&](Project &pr) {
            for (const Sel &x : sets)
                if (Graph *g = pr.graph(x.gid)) {
                    const int nid = duplicateSet(*g, x.sid);
                    if (nid >= 0)
                        made << Sel::set(x.gid, nid);
                }
        });
        if (!made.isEmpty())
            m_doc->selectSets(made);
    } else if (s.kind == Sel::Graph) {
        int nid = -1;
        m_doc->modify(T("Копия графика"), [&](Project &pr) {
            const Graph *g = pr.graph(s.gid);
            if (!g)
                return;
            Graph c = *g;
            c.id = pr.nextGraphId();
            Vec4 v = c.p.v4("view");
            c.p.setV4("view", v[0] + 0.05, v[1] - 0.05, v[2] + 0.05, v[3] - 0.05);
            pr.graphs.append(c);
            nid = c.id;
        });
        m_doc->select(Sel::graph(nid));
    } else if (s.kind == Sel::Object) {
        int ni = -1;
        m_doc->modify(T("Копия объекта"), [&](Project &pr) {
            if (s.obj < 0 || s.obj >= pr.objects.size())
                return;
            DrawObject o = pr.objects[s.obj];
            if (o.kind == "string") {
                Vec2 p = o.p.v2("pos");
                if (o.p.s("loctype") == "view")
                    o.p.setV2("pos", p[0] + 0.03, p[1] - 0.03);
            } else if (o.p.s("loctype") == "view") {
                Vec4 c = o.p.v4("coords");
                o.p.setV4("coords", c[0] + 0.03, c[1] - 0.03, c[2] + 0.03, c[3] - 0.03);
            }
            pr.objects.append(o);
            ni = pr.objects.size() - 1;
        });
        m_doc->select(Sel::object(ni));
    }
}

void MainWindow::addGraph()
{
    int nid = -1;
    m_doc->modify(T("Новый график"), [&](Project &pr) {
        Graph g(pr.nextGraphId());
        if (!pr.graphs.isEmpty()) {
            g = pr.graphs.first();
            g.id = pr.nextGraphId();
            g.sets.clear();
            g.p.set("title", QString());
            g.p.set("subtitle", QString());
            g.p.setV4("world", 0, 0, 1, 1);
            for (int ax = 0; ax < 4; ++ax) {
                g.axes[ax].p.set("tick_major", 0.2);
                g.axes[ax].p.set("label", QString());
                g.axes[ax].spec.clear();
                g.axes[ax].p.set("tick_spec_type", "none");
            }
            g.p.set("xscale", "Normal");
            g.p.set("yscale", "Normal");
            g.extra.clear();
        }
        pr.graphs.append(g);
        nid = g.id;
        // расставить все видимые графики в столбец/сетку
        const int n = pr.graphs.size();
        const int cols = n <= 2 ? 1 : 2;
        const int rows = (n + cols - 1) / cols;
        arrangeGraphs(pr, rows, cols, 0.15, 0.08, 0.1, 0.08, 0.12, 0.12, false);
    });
    m_doc->select(Sel::graph(nid));
}

void MainWindow::arrange()
{
    ArrangeDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    m_doc->modify(T("Расстановка графиков"), [&](Project &pr) {
        arrangeGraphs(pr, dlg.rows(), dlg.cols(), dlg.left(), dlg.right(), dlg.bottom(), dlg.top(), dlg.hgap(), dlg.vgap(), true);
    });
}

void MainWindow::autoscale(const QString &axes, int gid)
{
    if (gid < 0)
        gid = m_doc->currentGraph();
    const Sel s = m_doc->selection();
    QVector<int> only;
    m_doc->modify(T("Автомасштаб"), [&](Project &pr) {
        if (Graph *g = pr.graph(gid))
            if (!autoscaleGraph(*g, axes, only))
                statusBar()->showMessage(T("В графике нет данных"), 3000);
    });
    Q_UNUSED(s);
}

void MainWindow::editText(const Sel &s)
{
    const Project &pr = m_doc->project();
    QString text, title;
    int font = 0;
    PropTarget t;
    QString prop;
    if (s.kind == Sel::Title) {
        const Graph *g = pr.graph(s.gid);
        if (!g) return;
        t = {PropTarget::GraphP, s.gid};
        prop = "title";
        text = g->p.s("title");
        font = g->p.i("title_font");
        title = T("Заголовок графика G%1").arg(s.gid);
    } else if (s.kind == Sel::Axis) {
        const Graph *g = pr.graph(s.gid);
        if (!g) return;
        t = {PropTarget::AxisP, s.gid, -1, s.axis};
        prop = "label";
        text = g->axes[s.axis].p.s("label");
        font = g->axes[s.axis].p.i("label_font");
        title = T("Подпись оси");
    } else if (s.kind == Sel::Object && s.obj >= 0 && s.obj < pr.objects.size()) {
        t = {PropTarget::ObjectP, -1, -1, 0, s.obj};
        prop = "text";
        text = pr.objects[s.obj].p.s("text");
        font = pr.objects[s.obj].p.i("font");
        title = T("Текст");
    } else {
        return;
    }
    TextDialog dlg(m_doc, title, text, font, this);
    if (dlg.exec() == QDialog::Accepted)
        m_doc->setProp(t, prop, dlg.text(), title);
}

void MainWindow::showProperties(const Sel &s)
{
    m_inspDock->show();
    m_inspDock->raise();
    static const QHash<int, QString> field = {{Sel::Title, "title"}, {Sel::Axis, "label"}, {Sel::Object, "text"}};
    QString name = field.value(s.kind);
    if (s.kind == Sel::Set && m_doc->selectedSets().size() <= 1)
        name = "legend";
    if (s.kind == Sel::Object && m_doc->project().objects.value(s.obj).kind != "string")
        name.clear();
    if (!name.isEmpty())
        QTimer::singleShot(0, this, [this, name] { m_inspector->focusProperty(name); });
}

void MainWindow::moveLayer(int move)
{
    const Sel s = m_doc->selection();
    static const QHash<int, const char *> names = {{LayerFront, "На передний план"}, {LayerForward, "Вперёд"},
                                                   {LayerBackward, "Назад"}, {LayerBack, "На задний план"}};
    const QString label = T(names.value(move));
    if (s.kind == Sel::Set) {
        const QVector<Sel> sets = m_doc->selectedSets();
        QVector<Sel> after;
        m_doc->modify(label, [&](Project &pr) {
            QSet<int> gids;
            for (const Sel &x : sets)
                gids.insert(x.gid);
            for (int gid : gids) {
                Graph *g = pr.graph(gid);
                if (!g)
                    continue;
                QVector<int> order, sel;
                for (const DataSet &d : g->sets)
                    order << d.id;
                for (const Sel &x : sets)
                    if (x.gid == gid)
                        sel << x.sid;
                const auto map = reorderSets(*g, layerOrder(order, sel, move));
                for (int sid : sel)
                    after << Sel::set(gid, map.value(sid, sid));
            }
        });
        Sel primary = Sel::set(s.gid, -1);
        for (const Sel &x : after)
            if (x.gid == s.gid)
                primary = x;  // основной остаётся в своём графике
        m_doc->selectSets(after, primary);
    } else if (s.kind == Sel::Graph || s.kind == Sel::Legend || s.kind == Sel::Title || s.kind == Sel::Axis) {
        int ngid = s.gid;
        m_doc->modify(label, [&](Project &pr) {
            QVector<int> order;
            for (const Graph &g : pr.graphs)
                order << g.id;
            ngid = reorderGraphs(pr, layerOrder(order, {s.gid}, move)).value(s.gid, s.gid);
        });
        m_doc->select(Sel::graph(ngid));
    } else if (s.kind == Sel::Object) {
        int ni = s.obj;
        m_doc->modify(label, [&](Project &pr) {
            QVector<int> order;
            for (int i = 0; i < pr.objects.size(); ++i)
                order << i;
            const QVector<int> no = layerOrder(order, {s.obj}, move);
            QVector<DrawObject> objs;
            for (int i : no)
                objs << pr.objects[i];
            pr.objects = objs;
            ni = no.indexOf(s.obj);
        });
        m_doc->select(Sel::object(ni));
    } else {
        statusBar()->showMessage(T("Выделите набор, график или надпись"), 3000);
    }
}

void MainWindow::textData(const QString &text, const Sel &replace)
{
    TextDataDialog dlg(m_doc, text, replace, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    const QVector<Sel> sets = dlg.apply();
    if (!sets.isEmpty())
        m_doc->selectSets(sets);
}

// ⌘V на холсте: числа из буфера обмена становятся новым набором
void MainWindow::pasteData()
{
    const QString text = QApplication::clipboard()->text();
    if (parseDataText(text).points == 0) {
        statusBar()->showMessage(T("В буфере обмена нет чисел для нового набора"), 3000);
        return;
    }
    textData(text);
}

void MainWindow::applyStyleNamed(const QString &key)
{
    QString name = key;
    for (const StyleDef &sd : styleDefs())
        if (sd.key == key)
            name = T(sd.name);
    m_doc->modify(T("Стиль: %1").arg(name), [key](Project &pr) { applyStyle(pr, key); });
}

void MainWindow::styleGallery()
{
    StyleGalleryDialog dlg(m_doc, this);
    dlg.exec();
}

// Градиент по выделенным наборам (или по всем наборам текущего графика)
void MainWindow::addGradientActions(QMenu *menu)
{
    const QVector<QPair<QString, QString>> g = {{"viridis", T("Градиент viridis (фиолетовый → жёлтый)")},
                                                {"inferno", T("Градиент «тёплый» (чёрный → жёлтый)")},
                                                {"blues", T("Градиент синий")}};
    for (const auto &it : g) {
        const QString key = it.first;
        menu->addAction(it.second, this, [this, key] {
            QVector<QPair<int, int>> sets;
            for (const Sel &s : m_doc->selectedSets())
                sets << qMakePair(s.gid, s.sid);
            if (sets.size() < 2) {
                sets.clear();
                if (const Graph *gr = m_doc->project().graph(m_doc->currentGraph()))
                    for (const DataSet &d : gr->sets)
                        sets << qMakePair(gr->id, d.id);
            }
            m_doc->modify(T("Градиент цветов"), [&](Project &pr) { gradientColors(pr, sets, key); });
        });
    }
}

void MainWindow::setMath(const QString &op)
{
    auto *dlg = new SetMathDialog(m_doc, op, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

void MainWindow::quickSetMath(const SetMathSpec &spec)
{
    Project result = m_doc->project();
    const SetMathResult r = runSetMath(result, spec);
    if (!r.ok) {
        statusBar()->showMessage("⚠︎ " + r.message, 5000);
        return;
    }
    const int g = spec.many.isEmpty() ? spec.ga : spec.many[0].first;
    const int s = spec.many.isEmpty() ? spec.sa : spec.many[0].second;
    m_doc->modify(T("Операция: %1").arg(setMathLabel(m_doc->project(), spec, g, s)), [&](Project &pr) { pr = result; });
    if (!spec.replace) {
        QVector<Sel> sel;
        for (const auto &p : r.sets)
            sel << Sel::set(p.first, p.second);
        m_doc->selectSets(sel);
    }
    statusBar()->showMessage(r.message, 5000);
}

void MainWindow::selectAllSets()
{
    // все наборы текущего графика (или всех графиков, если выделена страница)
    QVector<Sel> sets;
    const Sel s = m_doc->selection();
    for (const Graph &g : m_doc->project().graphs) {
        if (s.kind != Sel::Page && s.kind != Sel::None && g.id != m_doc->currentGraph())
            continue;
        for (const DataSet &d : g.sets)
            sets << Sel::set(g.id, d.id);
    }
    m_doc->selectSets(sets);
}

void MainWindow::showData(int gid, int sid)
{
    if (gid < 0) {
        const Sel &s = m_doc->selection();
        if (s.kind == Sel::Set) {
            gid = s.gid;
            sid = s.sid;
        } else {
            const Graph *g = m_doc->project().graph(m_doc->currentGraph());
            if (g && !g->sets.isEmpty()) {
                gid = g->id;
                sid = g->sets.first().id;
            }
        }
    }
    if (gid >= 0) {
        m_doc->select(Sel::set(gid, sid));
        m_data->showSet(gid, sid);
    }
    m_dataDock->show();
    m_dataDock->raise();
}

void MainWindow::analysis(int tab)
{
    auto *dlg = new AnalysisDialog(m_doc, tab, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

void MainWindow::editTicks(int gid, int axis)
{
    TickSpecDialog dlg(m_doc, gid, axis, this);
    if (dlg.exec() == QDialog::Accepted)
        dlg.apply();
}

void MainWindow::moveSetTo(int gid, int sid, int target)
{
    m_doc->modify(T("Перенос набора"), [&](Project &pr) {
        Graph *g = pr.graph(gid), *t = pr.graph(target);
        if (!g || !t || !g->set(sid)) return;
        DataSet s = *g->set(sid);
        s.id = t->nextSetId();
        g->sets.erase(std::remove_if(g->sets.begin(), g->sets.end(), [&](const DataSet &d) { return d.id == sid; }), g->sets.end());
        t->sets.append(s);
    });
}

void MainWindow::showContextMenu(const Sel &s, const QPoint &pos)
{
    QMenu m(this);
    const Project &pr = m_doc->project();
    if (s.kind == Sel::Set) {
        const int gid = s.gid, sid = s.sid;
        const QVector<Sel> sets = m_doc->selectedSets().isEmpty() ? QVector<Sel>{s} : m_doc->selectedSets();
        const bool group = sets.size() > 1;
        auto forAll = [this, sets](const QString &label, const std::function<void(Project &, DataSet &, int)> &fn) {
            m_doc->modify(label, [&](Project &p) {
                for (const Sel &x : sets)
                    if (Graph *g = p.graph(x.gid))
                        if (DataSet *d = g->set(x.sid))
                            fn(p, *d, x.gid);
            });
        };
        if (group) {
            auto *t = m.addAction(T("Выбрано наборов: %1").arg(sets.size()));
            t->setEnabled(false);
            m.addSeparator();
        } else {
            m.addAction(icon("table"), T("Таблица данных"), this, [=] { showData(gid, sid); });
            m.addAction(icon("text"), T("Изменить данные как текст…"), this, [=] {
                const Graph *gg = m_doc->project().graph(gid);
                const DataSet *dd = gg ? gg->set(sid) : nullptr;
                if (dd)
                    textData(setToText(*dd), Sel::set(gid, sid));
            });
        }
        m.addAction(icon("inspector"), T("Свойства…"), this, [=] { showProperties(s); });
        const DataSet *d = pr.graph(gid) ? pr.graph(gid)->set(sid) : nullptr;
        if (d) {
            const bool hide = !d->hidden();
            m.addAction(hide ? T("Скрыть") : T("Показать"), this,
                        [=] { forAll(T("Видимость набора"), [hide](Project &, DataSet &x, int) { x.p.set("hidden", hide); }); });
        }
        QMenu *fm = m.addMenu(T("Заливка под кривой"));
        const QStringList fillNames = {T("Нет"), T("До оси X (y = 0)"), T("До нижнего края графика"), T("До верхнего края графика")};
        for (int mode = 0; mode < 4; ++mode)
            fm->addAction(fillNames[mode], this, [=] {
                forAll(T("Заливка под кривой"), [mode](Project &p, DataSet &x, int g) { fillToAxis(p, g, x.id, mode); });
            });
        if (group)
            m.addAction(icon("palette"), T("Разные цвета по порядку"), this, [=] {
                QVector<QPair<int, int>> v;
                for (const Sel &x : sets)
                    v << qMakePair(x.gid, x.sid);
                m_doc->modify(T("Разные цвета"), [&](Project &p) { distinctColors(p, v); });
            });
        if (group)
            addGradientActions(m.addMenu(T("Раскрасить градиентом")));
        {
            // арифметика над наборами
            QMenu *am = m.addMenu(icon("setmath"), T("Операции"));
            QVector<QPair<int, int>> pairs;
            for (const Sel &x : sets)
                pairs << qMakePair(x.gid, x.sid);
            auto quick = [=](const QString &op, bool replace) {
                SetMathSpec sp;
                sp.op = op;
                sp.ga = gid;
                sp.sa = sid;
                sp.many = pairs;
                sp.replace = replace;
                if (sets.size() == 2 && isBinaryMathOp(op)) {
                    sp.many.clear();
                    sp.ga = sets[0].gid;
                    sp.sa = sets[0].sid;
                    sp.gb = sets[1].gid;
                    sp.sb = sets[1].sid;
                }
                quickSetMath(sp);
            };
            am->addAction(icon("function"), T("Формула из наборов…"), this, [this] { setMath("expr"); });
            am->addSeparator();
            if (group) {
                am->addAction(T("Сумма выделенных"), this, [=] { quick("sum", false); });
                am->addAction(T("Среднее выделенных (± σ)"), this, [=] { quick("mean", false); });
                if (sets.size() == 2) {
                    const QString an = QString("S%1").arg(sets[0].sid), bn = QString("S%1").arg(sets[1].sid);
                    am->addAction(T("Разность %1 − %2").arg(an, bn), this, [=] { quick("sub", false); });
                    am->addAction(T("Отношение %1 ÷ %2").arg(an, bn), this, [=] { quick("div", false); });
                }
            } else {
                am->addAction(T("Сложить с другим набором…"), this, [this] { setMath("add"); });
                am->addAction(T("Вычесть другой набор…"), this, [this] { setMath("sub"); });
            }
            am->addSeparator();
            am->addAction(T("Умножить на число…"), this, [this] { setMath("mul:num"); });
            am->addAction(T("Прибавить число…"), this, [this] { setMath("add:num"); });
            am->addSeparator();
            am->addAction(T("Сменить знак (−y)"), this, [=] { quick("neg", true); });
            am->addAction(T("Нормировать на максимум"), this, [=] { quick("normmax", true); });
            am->addAction(T("Вычесть минимум"), this, [=] { quick("submin", true); });
            am->addSeparator();
            am->addAction(a["setmath"]);
        }
        m.addAction(icon("copy"), T("Дублировать"), this, [this] { duplicateSelection(); });
        if (pr.graphs.size() > 1) {
            QMenu *mv = m.addMenu(T("Перенести в график"));
            for (const Graph &g : pr.graphs)
                if (g.id != gid || group)
                    mv->addAction(g.displayName(), this, [=] {
                        for (const Sel &x : sets)
                            if (x.gid != g.id)
                                moveSetTo(x.gid, x.sid, g.id);
                    });
        }
        if (!group) {
            m.addSeparator();
            m.addAction(icon("function"), T("Формула…"), this, [this] { analysis(0); });
            m.addAction(icon("fit_curve"), T("Регрессия…"), this, [this] { analysis(2); });
            m.addAction(icon("fit_curve"), T("Аппроксимация…"), this, [this] { analysis(3); });
            m.addAction(icon("wand"), T("Преобразования…"), this, [this] { analysis(4); });
        }
        m.addSeparator();
        m.addAction(icon("autoscale"), group ? T("Масштаб по выбранным наборам") : T("Масштаб по этому набору"), this, [=] {
            QVector<int> ids;
            for (const Sel &x : sets)
                if (x.gid == gid)
                    ids << x.sid;
            m_doc->modify(T("Автомасштаб"), [&](Project &p) {
                if (Graph *g = p.graph(gid)) autoscaleGraph(*g, "xy", ids);
            });
        });
        m.addSeparator();
        QMenu *om = m.addMenu(T("Порядок слоёв"));
        om->addActions({a["front"], a["forward"], a["backward"], a["back"]});
        m.addAction(icon("trash"), group ? T("Удалить наборы") : T("Удалить набор"), this, [this] { deleteSelection(); });
    } else if (s.kind == Sel::Graph || s.kind == Sel::Legend || s.kind == Sel::Title || s.kind == Sel::Axis) {
        const int gid = s.gid;
        m.addAction(icon("autoscale"), T("Автомасштаб"), this, [=] { autoscale("xy", gid); });
        m.addAction(icon("import"), T("Импорт данных…"), this, [this] { fileImport(); });
        m.addAction(icon("text"), T("Набор из текста…"), this, [this] { textData(); });
        m.addAction(icon("function"), T("Новый набор по формуле…"), this, [this] { analysis(1); });
        if (s.kind == Sel::Title || s.kind == Sel::Axis)
            m.addAction(icon("text"), T("Изменить текст…"), this, [=] { editText(s); });
        m.addSeparator();
        QMenu *sc = m.addMenu(T("Шкалы"));
        for (int k = 0; k < 2; ++k) {
            const QString key = k ? "yscale" : "xscale";
            const Graph *g = pr.graph(gid);
            auto *x = sc->addAction(k ? T("Логарифмическая Y") : T("Логарифмическая X"));
            x->setCheckable(true);
            x->setChecked(g && g->p.s(key) == "Logarithmic");
            connect(x, &QAction::toggled, this, [=](bool on) {
                m_doc->modify(T("Шкала"), [&](Project &p) {
                    if (Graph *gg = p.graph(gid)) {
                        gg->p.set(key, on ? "Logarithmic" : "Normal");
                        autoscaleGraph(*gg, k ? "y" : "x");
                    }
                });
            });
        }
        m.addAction(icon("copy"), T("Дублировать график"), this, [=] {
            m_doc->select(Sel::graph(gid));
            duplicateSelection();
        });
        QMenu *gom = m.addMenu(T("Порядок слоёв"));
        gom->addActions({a["front"], a["forward"], a["backward"], a["back"]});
        m.addAction(icon("trash"), T("Удалить график"), this, [=] {
            m_doc->select(Sel::graph(gid));
            deleteSelection();
        });
    } else if (s.kind == Sel::Object) {
        const int idx = s.obj;
        if (idx >= 0 && idx < pr.objects.size() && pr.objects[idx].kind == "string")
            m.addAction(icon("text"), T("Изменить текст…"), this, [=] { editText(s); });
        QMenu *om = m.addMenu(T("Порядок слоёв"));
        om->addActions({a["front"], a["forward"], a["backward"], a["back"]});
        m.addAction(icon("copy"), T("Дублировать"), this, [this] { duplicateSelection(); });
        m.addAction(icon("trash"), T("Удалить"), this, [this] { deleteSelection(); });
    } else {
        m.addAction(a["addgraph"]);
        m.addAction(a["arrange"]);
        m.addAction(a["import"]);
        m.addAction(a["textdata"]);
        m.addSeparator();
        m.addAction(a["export"]);
        m.addAction(a["copyimg"]);
    }
    m.exec(pos);
}

void MainWindow::shotHook()
{
    // NEOGRACE_SHOT_SEL: set:G:S | graph:G | axis:G:A | legend:G | obj:N ; NEOGRACE_SHOT_DATA=1
    const QStringList p = qEnvironmentVariable("NEOGRACE_SHOT_SEL").split(':');
    if (p.value(0) == "set") m_doc->select(Sel::set(p.value(1).toInt(), p.value(2).toInt()));
    else if (p.value(0) == "graph") m_doc->select(Sel::graph(p.value(1).toInt()));
    else if (p.value(0) == "axis") m_doc->select(Sel::axisOf(p.value(1).toInt(), p.value(2).toInt()));
    else if (p.value(0) == "legend") m_doc->select(Sel::legend(p.value(1).toInt()));
    else if (p.value(0) == "obj") m_doc->select(Sel::object(p.value(1).toInt()));
    else if (p.value(0) == "sets") {  // sets:0.1,0.2
        QVector<Sel> v;
        for (const QString &gs : p.value(1).split(','))
            v << Sel::set(gs.section('.', 0, 0).toInt(), gs.section('.', 1, 1).toInt());
        m_doc->selectSets(v);
    }
    if (qEnvironmentVariableIsSet("NEOGRACE_SHOT_FILL")) {
        const int mode = qEnvironmentVariableIntValue("NEOGRACE_SHOT_FILL");
        const QVector<Sel> sets = m_doc->selectedSets();
        m_doc->modify("fill", [&](Project &pr) {
            for (const Sel &x : sets)
                fillToAxis(pr, x.gid, x.sid, mode);
        });
        m_doc->undo()->setClean();  // снимок экрана не должен спрашивать о сохранении
    }
    if (qEnvironmentVariableIsSet("NEOGRACE_SHOT_STYLE")) {
        Project p = m_doc->project();
        applyStyle(p, qEnvironmentVariable("NEOGRACE_SHOT_STYLE"));
        m_doc->restore(p);
    }
    if (qEnvironmentVariableIntValue("NEOGRACE_SHOT_DATA"))
        showData();
    const QString dlg = qEnvironmentVariable("NEOGRACE_SHOT_DIALOG");
    if (dlg == "fit") analysis(3);
    else if (dlg == "import") {
        auto *d = new ImportDialog(m_doc, qEnvironmentVariable("NEOGRACE_SHOT_FILE"), this);
        d->setAttribute(Qt::WA_DeleteOnClose);
        d->show();
    } else if (dlg == "gallery") {
        auto *d = new StyleGalleryDialog(m_doc, this);
        d->setAttribute(Qt::WA_DeleteOnClose);
        d->show();
    } else if (dlg == "setmath") {
        auto *d = new SetMathDialog(m_doc, qEnvironmentVariable("NEOGRACE_SHOT_OP"), this);
        d->setAttribute(Qt::WA_DeleteOnClose);
        d->show();
        if (qEnvironmentVariableIsSet("NEOGRACE_SHOT_EXPR"))
            d->setExpression(qEnvironmentVariable("NEOGRACE_SHOT_EXPR"));
        if (qEnvironmentVariableIntValue("NEOGRACE_SHOT_APPLY")) {
            d->apply();
            m_doc->undo()->setClean();
        }
    } else if (dlg == "textdata") {
        auto *d = new TextDataDialog(m_doc, qEnvironmentVariable("NEOGRACE_SHOT_TEXT").replace("\\n", "\n"), Sel(), this);
        d->setAttribute(Qt::WA_DeleteOnClose);
        d->show();
    } else if (dlg == "text") {
        auto *d = new TextDialog(m_doc, T("Текст"), "E = mc\\S2\\N, \\xD\\f{}T = 5 \\#{b0}C", 4, this);
        d->setAttribute(Qt::WA_DeleteOnClose);
        d->show();
    }
}

void MainWindow::about()
{
    QMessageBox::about(this, T("О программе NeoGrace"),
                       (QString("<h3>NeoGrace %1</h3>") + T("<p>Современный редактор графиков в формате Grace (.agr) — для xmgrace/QtGrace-проектов, нативно на Apple Silicon.</p><p>Открывает и сохраняет .agr без потери настроек, читает .xvg (GROMACS) и текстовые таблицы, экспортирует PDF, SVG, PNG.</p>") + QString("<p>Qt %2</p>"))
                           .arg(QApplication::applicationVersion(), qVersion()));
}

}  // namespace ng
