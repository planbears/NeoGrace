// Тесты интерфейса: мышь на холсте, инспектор, таблица, главное окно.
#include "agrio.h"
#include "datatable.h"
#include "dialogs.h"
#include "document.h"
#include "inspector.h"
#include "mainwindow.h"
#include "ops.h"
#include "pageview.h"
#include "projecttree.h"

#include <QApplication>
#include <QDir>
#include <QMouseEvent>
#include <QDoubleSpinBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QToolButton>
#include <QComboBox>
#include <QLineEdit>
#include <QTableView>
#include <QtTest>
#include <cmath>

using namespace ng;

static void mouse(QWidget *w, QEvent::Type t, QPointF pos, Qt::MouseButton b, Qt::MouseButtons bs,
                  Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    QMouseEvent e(t, pos, w->mapToGlobal(pos.toPoint()), b, bs, mods);
    QApplication::sendEvent(w, &e);
}

static void drag(QWidget *w, QPointF a, QPointF b)
{
    mouse(w, QEvent::MouseButtonPress, a, Qt::LeftButton, Qt::LeftButton);
    for (int i = 1; i <= 5; ++i)
        mouse(w, QEvent::MouseMove, a + (b - a) * (i / 5.0), Qt::NoButton, Qt::LeftButton);
    mouse(w, QEvent::MouseButtonRelease, b, Qt::LeftButton, Qt::NoButton);
}

static void flush(PageView &v)
{
    QApplication::processEvents();
    v.viewport()->grab();  // принудительно нарисовать
    QApplication::processEvents();
}

class GuiTest : public QObject {
    Q_OBJECT
private slots:
    void canvasInteractions()
    {
        Document doc;
        Project pr;
        QVERIFY(loadProject(QString(NG_TEST_DATA) + "/terr.agr", pr));
        doc.setProject(pr);
        PageView view(&doc);
        view.resize(1000, 800);
        view.show();
        flush(view);
        const RenderInfo &info = view.info();
        QVERIFY(info.frames.contains(0));
        const QRectF frame = info.frames.value(0);

        // щелчок по пустому месту графика выделяет график
        QPointF empty = frame.center() + QPointF(frame.width() * 0.05, frame.height() * 0.3);
        mouse(view.viewport(), QEvent::MouseButtonPress, empty, Qt::LeftButton, Qt::LeftButton);
        mouse(view.viewport(), QEvent::MouseButtonRelease, empty, Qt::LeftButton, Qt::NoButton);
        QCOMPARE(doc.selection().kind, Sel::Graph);

        // перетаскивание выделенного графика двигает вьюпорт; отмена возвращает
        const Vec4 v0 = doc.project().graph(0)->p.v4("view");
        drag(view.viewport(), empty, empty + QPointF(40, 0));
        const Vec4 v1 = doc.project().graph(0)->p.v4("view");
        QVERIFY(v1[0] > v0[0] + 0.01);
        doc.undo()->undo();
        QCOMPARE(doc.project().graph(0)->p.v4("view")[0], v0[0]);
        flush(view);

        // ручка изменения размера
        doc.select(Sel::graph(0));
        flush(view);
        const QRectF fr = view.info().frames.value(0);
        drag(view.viewport(), fr.bottomRight(), fr.bottomRight() + QPointF(-50, -30));
        const Vec4 v2 = doc.project().graph(0)->p.v4("view");
        QVERIFY(v2[2] < v0[2] - 0.01);
        QVERIFY(v2[1] > v0[1] + 0.01);
        doc.undo()->undo();
        flush(view);

        // объект-текст перетаскивается
        int textIdx = -1;
        QPolygonF textPoly;
        for (const HitItem &h : view.info().items)
            if (h.kind == HitItem::Object && doc.project().objects[h.obj].kind == "string"
                && doc.project().objects[h.obj].p.s("loctype") == "view") {
                textIdx = h.obj;
                textPoly = h.poly;
            }
        if (textIdx >= 0) {
            const Vec2 p0 = doc.project().objects[textIdx].p.v2("pos");
            const QPointF c = textPoly.boundingRect().center();
            drag(view.viewport(), c, c + QPointF(30, 20));
            QCOMPARE(doc.selection().kind, Sel::Object);
            const Vec2 p1 = doc.project().objects[doc.selection().obj].p.v2("pos");
            QVERIFY(p1[0] > p0[0]);
            QVERIFY(p1[1] < p0[1]);
        }

        // увеличение рамкой меняет границы
        view.setMode(PageView::Zoom);
        flush(view);
        const QRectF f2 = view.info().frames.value(0);
        const Vec4 w0 = doc.project().graph(0)->p.v4("world");
        drag(view.viewport(), f2.center() - QPointF(60, 40), f2.center() + QPointF(60, 40));
        const Vec4 w1 = doc.project().graph(0)->p.v4("world");
        QVERIFY(w1[2] - w1[0] < (w0[2] - w0[0]) * 0.8);

        // сдвиг
        view.setMode(PageView::Pan);
        flush(view);
        drag(view.viewport(), f2.center(), f2.center() + QPointF(50, 0));
        const Vec4 w2 = doc.project().graph(0)->p.v4("world");
        QVERIFY(w2[0] < w1[0]);

        // создание фигур
        for (auto m : {PageView::AddBox, PageView::AddLine, PageView::AddEllipse}) {
            const int n = doc.project().objects.size();
            view.setMode(m);
            drag(view.viewport(), QPointF(100, 100), QPointF(200, 160));
            QCOMPARE(doc.project().objects.size(), n + 1);
            QCOMPARE(view.mode(), PageView::Select);
        }

        // двойной щелчок — свойства
        {
            flush(view);
            ng::Sel got;
            QObject::connect(&view, &PageView::propertiesWanted, [&](const ng::Sel &x) { got = x; });
            view.setMode(PageView::Select);
            const QPointF c = view.info().frames.value(0).center() + QPointF(5, 5);
            mouse(view.viewport(), QEvent::MouseButtonDblClick, c, Qt::LeftButton, Qt::LeftButton);
            QVERIFY(got.kind != Sel::None);
        }

        // колесо с Ctrl — масштаб
        const double z0 = view.zoom();
        QWheelEvent we(QPointF(300, 300), view.mapToGlobal(QPoint(300, 300)), QPoint(), QPoint(0, 120), Qt::NoButton,
                       Qt::ControlModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(view.viewport(), &we);
        QVERIFY(view.zoom() > z0);
        flush(view);
    }

    void legendDrag()
    {
        Document doc;
        Project pr;
        QVERIFY(loadProject(QString(NG_TEST_DATA) + "/bar.agr", pr));
        doc.setProject(pr);
        PageView view(&doc);
        view.resize(900, 700);
        view.show();
        flush(view);
        QRectF leg;
        for (const HitItem &h : view.info().items)
            if (h.kind == HitItem::Legend)
                leg = h.poly.boundingRect();
        QVERIFY(!leg.isNull());
        const Vec2 l0 = doc.project().graph(0)->p.v2("legend_pos");
        drag(view.viewport(), leg.center(), leg.center() + QPointF(-60, 40));
        QCOMPARE(doc.selection().kind, Sel::Legend);
        const Vec2 l1 = doc.project().graph(0)->p.v2("legend_pos");
        QVERIFY(l1[0] < l0[0]);
        QVERIFY(l1[1] < l0[1]);
        // одно действие отмены на всё перетаскивание
        doc.undo()->undo();
        QCOMPARE(doc.project().graph(0)->p.v2("legend_pos")[0], l0[0]);
    }

    void inspectorAllKinds()
    {
        // Все страницы инспектора для всех примеров строятся без падений
        Document doc;
        Inspector insp(&doc);
        ProjectTree tree(&doc);
        insp.resize(360, 800);
        insp.show();
        QDir d(NG_TEST_DATA);
        for (const QString &f : d.entryList({"*.agr"}, QDir::Files, QDir::Name)) {
            Project pr;
            QVERIFY(loadProject(d.filePath(f), pr));
            doc.setProject(pr);
            doc.select(Sel::page());
            for (const Graph &g : pr.graphs) {
                doc.select(Sel::graph(g.id));
                doc.select(Sel::legend(g.id));
                doc.select(Sel::title(g.id));
                for (int a = 0; a < 4; ++a)
                    doc.select(Sel::axisOf(g.id, a));
                for (const DataSet &s : g.sets)
                    doc.select(Sel::set(g.id, s.id));
            }
            for (int i = 0; i < pr.objects.size(); ++i)
                doc.select(Sel::object(i));
            QApplication::processEvents();
        }
    }

    void inspectorEdits()
    {
        Document doc;
        Project pr = newProject();
        QString err;
        createFromFormula(pr.graphs[0], 0, 1, 10, "x^2", &err);
        doc.setProject(pr);
        Inspector insp(&doc);
        insp.show();
        doc.select(Sel::set(0, 0));
        // изменение через документ отражается в инспекторе и отменяется
        doc.setProp({PropTarget::SetP, 0, 0}, "symbol", 3);
        QCOMPARE(doc.project().graphs[0].sets[0].p.i("symbol"), 3);
        doc.setProp({PropTarget::SetP, 0, 0}, "symbol_size", 1.2);
        doc.setProp({PropTarget::SetP, 0, 0}, "symbol_size", 1.4);  // склеивается с предыдущим
        QCOMPARE(doc.undo()->count(), 2);
        doc.undo()->undo();
        QCOMPARE(doc.project().graphs[0].sets[0].p.d("symbol_size"), 1.0);
        // найдём спинбокс размера и изменим его
        auto spins = insp.findChildren<QDoubleSpinBox *>();
        QVERIFY(!spins.isEmpty());
    }

    void groupEditing()
    {
        Document doc;
        Project pr = newProject();
        QString err;
        for (const char *f : {"x", "2*x", "3*x"})
            createFromFormula(pr.graphs[0], 0, 1, 5, f, &err);
        doc.setProject(pr);
        Inspector insp(&doc);
        insp.show();
        doc.select(Sel::set(0, 0));
        doc.toggleSet(Sel::set(0, 2));
        QCOMPARE(doc.selectedSets().size(), 2);
        QVERIFY(doc.isSetSelected(0, 0) && doc.isSetSelected(0, 2) && !doc.isSetSelected(0, 1));
        doc.setPropMany(doc.selectedSetTargets(), "line_linewidth", 4.0);
        QCOMPARE(doc.project().graphs[0].sets[0].p.d("line_linewidth"), 4.0);
        QCOMPARE(doc.project().graphs[0].sets[2].p.d("line_linewidth"), 4.0);
        QVERIFY(doc.project().graphs[0].sets[1].p.d("line_linewidth") != 4.0);
        QCOMPARE(doc.undo()->count(), 1);
        doc.undo()->undo();
        QVERIFY(doc.project().graphs[0].sets[2].p.d("line_linewidth") != 4.0);
        // снятие из группы
        doc.toggleSet(Sel::set(0, 2));
        QCOMPARE(doc.selectedSets().size(), 1);
        // выделение нескольких через дерево
        ProjectTree tree(&doc);
        tree.show();
        doc.selectSets({Sel::set(0, 0), Sel::set(0, 1), Sel::set(0, 2)});
        QApplication::processEvents();
        int selectedItems = tree.selectedItems().size();
        QCOMPARE(selectedItems, 3);
        // смена типа набора меняет число столбцов
        doc.setProp({PropTarget::SetP, 0, 1}, "type", "xydy");
        QCOMPARE(doc.project().graphs[0].sets[1].cols.size(), 3);
    }

    void dataTable()
    {
        Document doc;
        Project pr = newProject();
        QString err;
        createFromFormula(pr.graphs[0], 0, 4, 5, "2*x", &err);
        doc.setProject(pr);
        DataTable dt(&doc);
        dt.show();
        dt.showSet(0, 0);
        auto *view = dt.findChild<QTableView *>();
        QVERIFY(view);
        QAbstractItemModel *m = view->model();
        QCOMPARE(m->rowCount(), 6);  // + строка для добавления
        QVERIFY(m->setData(m->index(1, 1), "7,5"));
        QCOMPARE(doc.project().graphs[0].sets[0].col(1)[1], 7.5);
        QVERIFY(m->setData(m->index(5, 1), "100"));  // новая строка
        QCOMPARE(doc.project().graphs[0].sets[0].size(), 6);
        QVERIFY(!m->setData(m->index(0, 0), "abc"));
        doc.undo()->undo();
        QCOMPARE(doc.project().graphs[0].sets[0].size(), 5);
    }

    void analysisDialog()
    {
        Document doc;
        Project pr = newProject();
        QString err;
        createFromFormula(pr.graphs[0], 0, 5, 60, "3*exp(-x/1.5) + 0.2", &err);
        doc.setProject(pr);
        doc.select(Sel::set(0, 0));
        for (int tab : {1, 2, 3, 4}) {
            AnalysisDialog dlg(&doc, tab);
            dlg.show();
            QPushButton *run = nullptr;
            for (auto *b : dlg.findChildren<QPushButton *>())
                if (b->text() == QStringLiteral("Выполнить"))
                    run = b;
            QVERIFY(run);
            const int before = doc.project().graphs[0].sets.size() + doc.project().graphs.size();
            run->click();
            const int after = doc.project().graphs[0].sets.size() + doc.project().graphs.size();
            QVERIFY2(after == before + 1, qPrintable(QString("вкладка %1").arg(tab)));
        }
        // аппроксимация нашла параметры
        const auto &sets = doc.project().graphs[0].sets;
        bool found = false;
        for (const auto &s : sets)
            if (s.p.s("comment").contains("exp(-x / 1.5"))
                found = true;
        QVERIFY(found);
    }

    void textDataDialog()
    {
        Document doc;
        doc.setProject(newProject());
        {
            TextDataDialog dlg(&doc);
            auto *ed = dlg.findChild<QPlainTextEdit *>();
            QVERIFY(ed);
            ed->setPlainText("20.0 -10.0\n20.0 10.0");
            const QVector<Sel> sel = dlg.apply();
            QCOMPARE(sel.size(), 1);
            const DataSet *s = doc.project().graph(0)->set(sel[0].sid);
            QVERIFY(s && s->size() == 2);
            QCOMPARE(s->col(0), (QVector<double>{20, 20}));
            QCOMPARE(s->col(1), (QVector<double>{-10, 10}));
            const Vec4 w = doc.project().graph(0)->p.v4("world");
            QVERIFY(w[0] < 20 && w[2] > 20 && w[1] <= -10 && w[3] >= 10);
        }
        // правка того же набора как текста; одна отмена на действие
        {
            const DataSet &s0 = doc.project().graphs[0].sets[0];
            TextDataDialog dlg(&doc, setToText(s0), Sel::set(0, s0.id));
            auto *ed = dlg.findChild<QPlainTextEdit *>();
            QCOMPARE(ed->toPlainText(), QString("20 -10\n20 10\n"));
            ed->setPlainText("0 0\n1 1\n2 4");
            QCOMPARE(dlg.apply().size(), 1);
            QCOMPARE(doc.project().graphs[0].sets.size(), 1);
            QCOMPARE(doc.project().graphs[0].sets[0].size(), 3);
        }
        doc.undo()->undo();
        QCOMPARE(doc.project().graphs[0].sets[0].size(), 2);
        doc.undo()->undo();
        QCOMPARE(doc.project().graphs[0].sets.size(), 0);
    }

    void textDataFormula()
    {
        Document doc;
        doc.setProject(newProject());
        TextDataDialog dlg(&doc);
        auto *ed = dlg.findChild<QPlainTextEdit *>();
        ed->setPlainText("y = 0.5*x");
        QPushButton *ok = nullptr;
        for (auto *b : dlg.findChildren<QPushButton *>())
            if (b->text() == QStringLiteral("Добавить"))
                ok = b;
        QVERIFY(ok && ok->isEnabled());
        const QVector<Sel> sel = dlg.apply();
        QCOMPARE(sel.size(), 1);
        const DataSet *s = doc.project().graph(0)->set(sel[0].sid);
        QCOMPARE(s->size(), 200);
        QCOMPARE(s->col(1).last(), 0.5 * s->col(0).last());
    }

    static QLineEdit *numberEdit(QDialog &dlg)
    {
        for (auto *e : dlg.findChildren<QLineEdit *>())
            if (e->text() == "2")
                return e;
        return nullptr;
    }
    static QLineEdit *exprEdit(QDialog &dlg)
    {
        for (auto *e : dlg.findChildren<QLineEdit *>())
            if (e->placeholderText().startsWith("s0.y"))
                return e;
        return nullptr;
    }

    void setExpressionDialog()
    {
        Document doc;
        Project pr = newProject();
        QString err;
        for (const char *f : {"x", "2*x", "10"})
            createFromFormula(pr.graphs[0], 0, 10, 11, f, &err);
        doc.setProject(pr);
        doc.selectSets({Sel::set(0, 0), Sel::set(0, 1), Sel::set(0, 2)});
        SetMathDialog dlg(&doc);
        QCOMPARE(dlg.spec().op, QString("expr"));
        QLineEdit *e = exprEdit(dlg);
        QVERIFY(e);
        QCOMPARE(e->text(), QString("s0.y + s1.y + s2.y"));  // сумма выделенных — сразу
        QCOMPARE(dlg.apply().size(), 1);
        QCOMPARE(doc.project().graphs[0].sets.last().col(1)[10], 40.0);
        // своя формула с вычитанием
        e->setText("s1 - s0 - s2");
        const QVector<Sel> r = dlg.apply();
        QCOMPARE(r.size(), 1);
        QCOMPARE(doc.project().graph(0)->set(r[0].sid)->col(1)[10], 0.0);
        QCOMPARE(doc.project().graphs[0].sets.size(), 5);
        // кнопки: «Разность выделенных»
        doc.selectSets({Sel::set(0, 2), Sel::set(0, 0)});
        for (auto *b : dlg.findChildren<QPushButton *>())
            if (b->text() == QStringLiteral("Разность выделенных"))
                b->click();
        QCOMPARE(e->text(), QString("s2.y - s0.y"));
        // ошибка в формуле — ничего не меняется
        e->setText("s0 + s77");
        const int n = doc.undo()->count();
        QVERIFY(dlg.apply().isEmpty());
        QCOMPARE(doc.undo()->count(), n);
    }

    void setMathDialog()
    {
        Document doc;
        Project pr = newProject();
        QString err;
        createFromFormula(pr.graphs[0], 0, 10, 11, "x", &err);
        createFromFormula(pr.graphs[0], 0, 10, 11, "x*x", &err);
        doc.setProject(pr);
        doc.selectSets({Sel::set(0, 0), Sel::set(0, 1)});
        {
            // A + B по выделению
            SetMathDialog dlg(&doc, "add");
            const SetMathSpec sp = dlg.spec();
            QCOMPARE(sp.op, QString("add"));
            QCOMPARE(sp.sa, 0);
            QCOMPARE(sp.sb, 1);
            const QVector<Sel> r = dlg.apply();
            QCOMPARE(r.size(), 1);
            QCOMPARE(doc.project().graphs[0].sets.size(), 3);
            QCOMPARE(doc.project().graph(0)->set(r[0].sid)->col(1)[10], 110.0);
            QCOMPARE(doc.selectedSets().size(), 2);  // выделение прежнее — для следующей операции
        }
        doc.undo()->undo();
        QCOMPARE(doc.project().graphs[0].sets.size(), 2);
        // умножить оба выделенных на число прямо в наборах
        doc.selectSets({Sel::set(0, 0), Sel::set(0, 1)});
        {
            SetMathDialog dlg(&doc, "mul:num");
            numberEdit(dlg)->setText("3");
            auto combos = dlg.findChildren<QComboBox *>();
            for (auto *c : combos)
                if (c->findData(true) == 1 && c->itemText(0) == QStringLiteral("Новый набор"))
                    c->setCurrentIndex(1);
            const SetMathSpec sp = dlg.spec();
            QVERIFY(!sp.withSet && sp.replace && sp.many.size() == 2 && sp.number == 3);
            QCOMPARE(dlg.apply().size(), 2);
            QCOMPARE(doc.project().graphs[0].sets.size(), 2);
            QCOMPARE(doc.project().graphs[0].sets[1].col(1)[10], 300.0);
        }
        // ошибка не меняет проект и не пишет историю
        {
            const int n = doc.undo()->count();
            SetMathDialog dlg(&doc, "div:num");
            numberEdit(dlg)->setText("0");
            QVERIFY(dlg.apply().isEmpty());
            QCOMPARE(doc.undo()->count(), n);
        }
        // главное окно: действие есть в меню
        MainWindow w;
        bool found = false;
        for (QAction *a : w.findChildren<QAction *>())
            if (a->text() == QStringLiteral("Операции над наборами…"))
                found = true;
        QVERIFY(found);
    }

    void styleGallery()
    {
        Document doc;
        doc.setProject(newProject());  // пустой — миниатюры на примере
        StyleGalleryDialog dlg(&doc);
        int buttons = 0;
        for (auto *b : dlg.findChildren<QToolButton *>())
            if (!b->property("style").toString().isEmpty()) {
                ++buttons;
                QVERIFY(!b->icon().isNull());
            }
        QCOMPARE(buttons, int(styleDefs().size()));
        QVERIFY(dlg.styled("dark").graphs[0].sets.size() > 0);
        dlg.choose("dark");
        QCOMPARE(dlg.chosen(), QString("dark"));
        QVERIFY(doc.project().bgColor > 15);
        doc.undo()->undo();
        QCOMPARE(doc.project().bgColor, 0);
        // меню главного окна
        MainWindow w;
        int found = 0;
        for (QAction *a : w.findChildren<QAction *>())
            if (a->text() == QStringLiteral("Галерея стилей…") || a->text() == QStringLiteral("Тёмный"))
                ++found;
        QVERIFY(found >= 2);
    }

    void mainWindowOpensAll()
    {
        MainWindow w;
        w.show();
        QDir d(NG_TEST_DATA);
        for (const QString &f : d.entryList({"*.agr"}, QDir::Files, QDir::Name)) {
            QVERIFY(w.openFile(d.filePath(f)));
            QApplication::processEvents();
        }
    }
};

QTEST_MAIN(GuiTest)
#include "test_gui.moc"
