// Небольшие виджеты: кнопка выбора цвета, сворачиваемая секция.
#pragma once

#include "model.h"

#include <QDoubleSpinBox>
#include <QToolButton>
#include <QWidget>

class QVBoxLayout;
class QFormLayout;

namespace ng {

class Document;

// Выбор цвета из карты цветов проекта (+ «Другой…» добавляет новый цвет)
class ColorButton : public QToolButton {
    Q_OBJECT
public:
    ColorButton(Document *doc, QWidget *parent = nullptr);
    void setIndex(int idx);
    int index() const { return m_idx; }
signals:
    void picked(int idx);

private:
    void showPopup();
    Document *m_doc;
    int m_idx = 1;
};

// Число в «человеческом» виде (без лишних нулей), любой порядок величины
class GSpinBox : public QDoubleSpinBox {
    Q_OBJECT
public:
    explicit GSpinBox(QWidget *parent = nullptr);
    QString textFromValue(double v) const override;
    double valueFromText(const QString &t) const override;
    QValidator::State validate(QString &input, int &pos) const override;
};

class Section : public QWidget {
    Q_OBJECT
public:
    Section(const QString &title, QWidget *parent = nullptr, bool expanded = true);
    QFormLayout *form() const { return m_form; }
    QVBoxLayout *body() const { return m_body; }
    void setExpanded(bool e);
    bool isExpanded() const { return m_expanded; }

private:
    QToolButton *m_header;
    QWidget *m_content;
    QVBoxLayout *m_body;
    QFormLayout *m_form;
    bool m_expanded = true;
};

}  // namespace ng
