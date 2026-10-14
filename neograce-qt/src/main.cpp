// NeoGrace — современный редактор графиков Grace (.agr).
//
//   NeoGrace [файлы…]                       — открыть в окне
//   NeoGrace --export out.pdf in.agr        — пакетный экспорт без окна
//            [--dpi 300] [--transparent]     (форматы: pdf svg png jpg tiff;
//                                              .agr — преобразовать xvg/таблицу в проект)
#include "agrio.h"
#include "i18n.h"
#include "export.h"
#include "mainwindow.h"
#include "ops.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QFileOpenEvent>
#include <QPalette>
#include <QStyleFactory>
#include <QStyleHints>
#include <QLibraryInfo>
#include <QLocale>
#include <QSettings>
#include <QTimer>
#include <QTranslator>
#include <cstdio>

using namespace ng;

class NgApp : public QApplication {
public:
    NgApp(int &argc, char **argv) : QApplication(argc, argv) {}
    QStringList pending;
    bool ready = false;

protected:
    bool event(QEvent *e) override
    {
        if (e->type() == QEvent::FileOpen) {
            const QString f = static_cast<QFileOpenEvent *>(e)->file();
            if (ready)
                openInWindow(f);
            else
                pending << f;
            return true;
        }
        return QApplication::event(e);
    }

public:
    static void openInWindow(const QString &f)
    {
        // пустое главное окно используем повторно
        for (QWidget *w : QApplication::topLevelWidgets()) {
            auto *mw = qobject_cast<MainWindow *>(w);
            if (mw && mw->isVisible() && mw->windowFilePath().isEmpty() && !mw->isWindowModified()
                && mw->property("pristine").toBool()) {
                mw->setProperty("pristine", false);
                mw->openFile(f);
                mw->raise();
                mw->activateWindow();
                return;
            }
        }
        MainWindow::newWindow()->openFile(f);
    }
};

static const char *kStyle = R"(
QToolButton#sectionHeader {
    border: none; border-top: 1px solid palette(midlight);
    padding: 7px 8px 5px 6px; text-align: left; font-size: 12px;
    color: palette(text); background: transparent;
}
QToolButton#sectionHeader:hover { background: palette(alternate-base); }
QLabel#subtle { color: palette(placeholder-text); font-size: 11px; }
QLabel#mono { font-family: "Menlo", "DejaVu Sans Mono", monospace; font-size: 11px; }
QLabel#result { font-family: "Menlo", "DejaVu Sans Mono", monospace; font-size: 11px;
                background: palette(base); border: 1px solid palette(midlight); border-radius: 6px; padding: 6px; }
QLabel#textPreview { background: white; border: 1px solid palette(midlight); border-radius: 6px; }
QToolButton#segment { padding: 4px 8px; border: 1px solid palette(mid); background: palette(button); }
QToolButton#segment:checked { background: palette(highlight); color: palette(highlighted-text); border-color: palette(highlight); }
QDockWidget::title { padding: 6px 8px; font-weight: 600; }
QTreeWidget { font-size: 12px; }
QStatusBar { font-size: 11px; }
)";

static int runExport(const QStringList &files, const QString &out, double dpi, bool transparent)
{
    if (files.isEmpty()) {
        std::fprintf(stderr, "%s\n", qPrintable(T("Нужен входной файл .agr")));
        return 2;
    }
    Project pr;
    QString err;
    if (!loadProject(files.first(), pr, &err)) {
        std::fprintf(stderr, "%s\n", qPrintable(T("Не удалось открыть %1: %2").arg(files.first(), err)));
        return 1;
    }
    const bool ok = out.endsWith(".agr", Qt::CaseInsensitive) ? saveProject(pr, out, &err)
                                                               : exportFile(pr, out, dpi, transparent, &err);
    if (!ok) {
        std::fprintf(stderr, "%s\n", qPrintable(T("Не удалось сохранить %1: %2").arg(out, err)));
        return 1;
    }
    std::printf("%s → %s\n", qPrintable(files.first()), qPrintable(out));
    return 0;
}

int main(int argc, char **argv)
{
    // пакетный режим без окон
    bool batch = false;
    for (int i = 1; i < argc; ++i)
        if (QByteArray(argv[i]) == "--export" || QByteArray(argv[i]) == "-hardcopy")
            batch = true;
    if (batch && qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");

    QApplication::setAttribute(Qt::AA_DontShowIconsInMenus, true);
    NgApp app(argc, argv);
    QApplication::setApplicationName("NeoGrace");
    QApplication::setOrganizationName("NeoGrace");
    QApplication::setOrganizationDomain("neograce.org");
    QApplication::setApplicationVersion("1.0.0");
    QApplication::setWindowIcon(QIcon(":/icons/app.png"));

    // язык интерфейса: настройка пользователя, иначе язык системы
    {
        const QString lang = qEnvironmentVariable("NEOGRACE_LANG", QSettings().value("language").toString());
        const QLocale::Language sys = QLocale::system().language();
        const bool ru = lang == "ru" || (lang.isEmpty() && (sys == QLocale::Russian || sys == QLocale::Ukrainian
                                                            || sys == QLocale::Belarusian || sys == QLocale::Kazakh));
        setLanguage(ru ? Lang::Ru : Lang::En);
    }

    QCommandLineParser cli;
    cli.setApplicationDescription(T("Редактор графиков Grace (.agr)"));
    cli.addHelpOption();
    cli.addVersionOption();
    QCommandLineOption exportOpt({"export", "hardcopy"}, T("Сохранить картинку в <файл> и выйти."), T("файл"));
    QCommandLineOption dpiOpt("dpi", T("Разрешение растрового экспорта."), "dpi", "300");
    QCommandLineOption trOpt("transparent", T("Прозрачный фон."));
    cli.addOptions({exportOpt, dpiOpt, trOpt});
    cli.addPositionalArgument("files", T("Файлы .agr, .xvg или таблицы."), T("[файлы…]"));
    cli.process(app);

    if (cli.isSet(exportOpt))
        return runExport(cli.positionalArguments(), cli.value(exportOpt), cli.value(dpiOpt).toDouble(), cli.isSet(trOpt));

    // стандартные кнопки Qt по-русски
    auto *tr = new QTranslator(&app);
    if (!isEnglish()
        && (tr->load("qtbase_ru", QLibraryInfo::path(QLibraryInfo::TranslationsPath))
            || tr->load("qtbase_ru", QApplication::applicationDirPath() + "/../Resources/translations")))
        app.installTranslator(tr);

#ifndef Q_OS_MACOS
    app.setStyle(QStyleFactory::create("Fusion"));
#endif
    if (qEnvironmentVariableIntValue("NEOGRACE_DARK")) {  // тёмная тема вне macOS
        app.setStyle(QStyleFactory::create("Fusion"));
        QPalette p;
        p.setColor(QPalette::Window, QColor(40, 40, 43));
        p.setColor(QPalette::WindowText, QColor(230, 230, 235));
        p.setColor(QPalette::Base, QColor(28, 28, 30));
        p.setColor(QPalette::AlternateBase, QColor(46, 46, 50));
        p.setColor(QPalette::Text, QColor(230, 230, 235));
        p.setColor(QPalette::Button, QColor(55, 55, 60));
        p.setColor(QPalette::ButtonText, QColor(230, 230, 235));
        p.setColor(QPalette::Highlight, QColor(10, 132, 255));
        p.setColor(QPalette::HighlightedText, Qt::white);
        p.setColor(QPalette::Mid, QColor(70, 70, 75));
        p.setColor(QPalette::Midlight, QColor(60, 60, 65));
        p.setColor(QPalette::PlaceholderText, QColor(150, 150, 155));
        p.setColor(QPalette::ToolTipBase, QColor(55, 55, 60));
        p.setColor(QPalette::ToolTipText, QColor(230, 230, 235));
        app.setPalette(p);
    }
    app.setStyleSheet(QString::fromUtf8(kStyle));

    MainWindow *w = MainWindow::newWindow();
    w->setProperty("pristine", true);
    const QStringList files = cli.positionalArguments();
    bool first = true;
    for (const QString &f : files) {
        if (first) {
            w->setProperty("pristine", false);
            w->openFile(f);
            first = false;
        } else {
            MainWindow::newWindow()->openFile(f);
        }
    }
    app.ready = true;
    // файлы, пришедшие из Finder до запуска окна
    QTimer::singleShot(0, [&app] {
        for (const QString &f : app.pending)
            NgApp::openInWindow(f);
        app.pending.clear();
    });
    // отладка: NEOGRACE_SHOT=файл.png — снимок окна и выход
    const QString shot = qEnvironmentVariable("NEOGRACE_SHOT");
    if (!shot.isEmpty()) {
        QTimer::singleShot(qEnvironmentVariableIntValue("NEOGRACE_SHOT_DELAY") > 0
                               ? qEnvironmentVariableIntValue("NEOGRACE_SHOT_DELAY") : 1500,
                           [w, shot] {
                               w->shotHook();
                               QTimer::singleShot(600, [w, shot] {
                                   w->grab().save(shot);
                                   for (QWidget *t : QApplication::topLevelWidgets())
                                       if (t != w && t->isVisible() && t->inherits("QDialog"))
                                           t->grab().save(QString(shot).replace(".png", "_dlg.png"));
                                   qApp->quit();
                               });
                           });
    }
    return app.exec();
}
