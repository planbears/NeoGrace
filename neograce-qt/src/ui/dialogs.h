// Диалоги: импорт, анализ данных, расстановка графиков, экспорт, особые
// деления, правка текста с предпросмотром.
#pragma once

#include "agrio.h"
#include "document.h"
#include "setmath.h"

#include <QDialog>

class QComboBox;
class QSpinBox;
class QDoubleSpinBox;
class QCheckBox;
class QLineEdit;
class QPlainTextEdit;
class QTabWidget;
class QLabel;
class QTableWidget;
class QFormLayout;
class QPushButton;
class QRadioButton;
class QToolButton;

namespace ng {

// ---- импорт таблицы
class ImportDialog : public QDialog {
    Q_OBJECT
public:
    ImportDialog(Document *doc, const QString &path, QWidget *parent = nullptr);
    bool ok() const { return !m_tables.isEmpty(); }
    // применить к документу; вернуть id графика
    int apply();

private:
    void updatePreview();
    Document *m_doc;
    QString m_path, m_text;
    QVector<Table> m_tables;
    QComboBox *m_graph, *m_mode, *m_type, *m_block;
    QVector<QComboBox *> m_colBoxes;
    QCheckBox *m_autoscale, *m_blank;
    QLabel *m_info;
    QPlainTextEdit *m_preview;
    QWidget *m_colsBox;
    QFormLayout *m_form = nullptr;
};

// ---- анализ данных (формулы, регрессия, аппроксимация, преобразования)
class AnalysisDialog : public QDialog {
    Q_OBJECT
public:
    AnalysisDialog(Document *doc, int tab = 0, QWidget *parent = nullptr);

private:
    void run();
    void fillSets();
    Document *m_doc;
    QTabWidget *m_tabs;
    QComboBox *m_set;
    QLabel *m_result;
    // формула
    QPlainTextEdit *m_formula;
    // новый набор
    QLineEdit *m_newY, *m_newX;
    QDoubleSpinBox *m_from, *m_to;
    QSpinBox *m_npts;
    // регрессия
    QComboBox *m_regKind;
    QSpinBox *m_degree;
    // аппроксимация
    QLineEdit *m_fitExpr;
    QTableWidget *m_params;
    QSpinBox *m_iter;
    QCheckBox *m_fitRange;
    QDoubleSpinBox *m_fitX1, *m_fitX2;
    // преобразования
    QComboBox *m_op;
    QSpinBox *m_window, *m_bins, *m_interpN;
    QCheckBox *m_normalize, *m_power, *m_spline, *m_desc, *m_byY;
};

// ---- операции над наборами: A + B, A × число, сумма и среднее выделенных,
// смена знака, нормировка…  Результат — новый набор или изменённый исходный.
class SetMathDialog : public QDialog {
    Q_OBJECT
public:
    // op — начальная операция (add, mul, sum, neg…); «mul:num» — сразу с числом
    SetMathDialog(Document *doc, const QString &op = QString(), QWidget *parent = nullptr);
    // выполнить; вернуть новые или изменённые наборы (пусто — ошибка, она показана в окне)
    QVector<Sel> apply();
    SetMathSpec spec() const;
    void setExpression(const QString &text);  // формула из наборов

private:
    void fillSets();
    void update();
    Document *m_doc;
    QComboBox *m_op, *m_a, *m_b, *m_axis, *m_result;
    QRadioButton *m_useSet, *m_useNum;
    QLineEdit *m_num;
    QCheckBox *m_all, *m_std;
    QLabel *m_many, *m_preview;
    QFormLayout *m_form;
    QWidget *m_bRow;
    QPushButton *m_run = nullptr;
    QVector<Sel> m_selected;  // выделенные наборы
    // формула из наборов
    QWidget *m_exprBox = nullptr;
    QLineEdit *m_expr = nullptr;
    QLabel *m_exprHint = nullptr;
    QToolButton *m_insertSet = nullptr;
    QList<QPushButton *> m_templ;
    QComboBox *m_grid = nullptr;
    int m_home = 0;  // график, к которому относятся «sN»
    void insertText(const QString &t, bool asOperand);
};

// ---- галерея стилей: проект в каждом стиле оформления; щелчок — применить
class StyleGalleryDialog : public QDialog {
    Q_OBJECT
public:
    explicit StyleGalleryDialog(Document *doc, QWidget *parent = nullptr);
    void choose(const QString &style);  // применить стиль (и палитру) и закрыть
    QString chosen() const { return m_chosen; }
    Project styled(const QString &style) const;  // как будет выглядеть

private:
    void render();
    Document *m_doc;
    QComboBox *m_palette;
    QVector<QToolButton *> m_buttons;
    QString m_chosen;
};

// ---- набор, набранный текстом: столбцы чисел («20 -10⏎20 10» — отрезок)
class TextDataDialog : public QDialog {
    Q_OBJECT
public:
    // replace — заменить данные этого набора вместо создания нового
    TextDataDialog(Document *doc, const QString &text = QString(), const Sel &replace = Sel(),
                   QWidget *parent = nullptr);
    // применить; вернуть выделение новых/изменённых наборов
    QVector<Sel> apply();

private:
    void update();
    Document *m_doc;
    Sel m_replace;
    QPlainTextEdit *m_text;
    QComboBox *m_graph, *m_type, *m_target;
    QLineEdit *m_legend;
    QCheckBox *m_fit;
    QLabel *m_info;
    QPushButton *m_ok = nullptr;
    // диапазон для строк «y = f(x)»
    QFormLayout *m_form = nullptr;
    QWidget *m_range = nullptr;
    QLineEdit *m_from, *m_to;
    QSpinBox *m_npts;
};

class ArrangeDialog : public QDialog {
    Q_OBJECT
public:
    ArrangeDialog(QWidget *parent = nullptr);
    int rows() const;
    int cols() const;
    double hgap() const;
    double vgap() const;
    double left() const;
    double right() const;
    double top() const;
    double bottom() const;

private:
    QSpinBox *m_rows, *m_cols;
    QDoubleSpinBox *m_hgap, *m_vgap, *m_left, *m_right, *m_top, *m_bottom;
};

class ExportDialog : public QDialog {
    Q_OBJECT
public:
    ExportDialog(const QString &suggested, QWidget *parent = nullptr);
    QString format() const;
    double dpi() const;
    bool transparent() const;

private:
    QComboBox *m_fmt;
    QSpinBox *m_dpi;
    QCheckBox *m_transparent;
};

class TickSpecDialog : public QDialog {
    Q_OBJECT
public:
    TickSpecDialog(Document *doc, int gid, int axis, QWidget *parent = nullptr);
    void apply();

private:
    Document *m_doc;
    int m_gid, m_axis;
    QTableWidget *m_table;
    QComboBox *m_mode;
};

class TextDialog : public QDialog {
    Q_OBJECT
public:
    TextDialog(Document *doc, const QString &title, const QString &text, int font, QWidget *parent = nullptr);
    QString text() const;

private:
    void updatePreview();
    void insert(const QString &s);
    Document *m_doc;
    int m_font;
    QLineEdit *m_edit;
    QLabel *m_preview;
};

}  // namespace ng
