#include "widgets.h"
#include "i18n.h"

#include "document.h"
#include "icons.h"

#include <QColorDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>
#include <QWidgetAction>

namespace ng {

ColorButton::ColorButton(Document *doc, QWidget *parent) : QToolButton(parent), m_doc(doc)
{
    setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    setPopupMode(QToolButton::InstantPopup);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setAutoRaise(false);
    connect(this, &QToolButton::clicked, this, &ColorButton::showPopup);
    setIndex(1);
}

void ColorButton::setIndex(int idx)
{
    m_idx = idx;
    const Project &pr = m_doc->project();
    const QColor c = pr.color(idx);
    setIcon(colorIcon(c));
    QString name = pr.colors.contains(idx) ? pr.colors[idx].name : QString();
    if (name.isEmpty())
        name = c.name();
    setText(QString("%1  %2").arg(idx).arg(name));
}

void ColorButton::showPopup()
{
    QMenu menu(this);
    auto *w = new QWidget(&menu);
    auto *grid = new QGridLayout(w);
    grid->setContentsMargins(8, 8, 8, 8);
    grid->setSpacing(4);
    const Project &pr = m_doc->project();
    int k = 0;
    for (auto it = pr.colors.begin(); it != pr.colors.end(); ++it, ++k) {
        auto *b = new QToolButton(w);
        b->setIcon(colorIcon(it->rgb));
        b->setIconSize(QSize(18, 18));
        b->setAutoRaise(true);
        b->setToolTip(QString("%1: %2").arg(it.key()).arg(it->name));
        if (it.key() == m_idx)
            b->setDown(true);
        const int idx = it.key();
        connect(b, &QToolButton::clicked, this, [this, idx, &menu] {
            menu.close();
            setIndex(idx);
            emit picked(idx);
        });
        grid->addWidget(b, k / 8, k % 8);
    }
    auto *more = new QPushButton(T("Другой цвет…"), w);
    grid->addWidget(more, (k + 7) / 8 + 1, 0, 1, 8);
    connect(more, &QPushButton::clicked, this, [this, &menu] {
        menu.close();
        QColor c = QColorDialog::getColor(m_doc->project().color(m_idx), window(), T("Цвет"));
        if (!c.isValid())
            return;
        int idx = -1;
        for (auto it = m_doc->project().colors.begin(); it != m_doc->project().colors.end(); ++it)
            if (it->rgb.rgb() == c.rgb())
                idx = it.key();
        if (idx < 0) {
            m_doc->modify(T("Новый цвет"), [&](Project &pr) { idx = pr.addColor(c); });
        }
        setIndex(idx);
        emit picked(idx);
    });
    auto *act = new QWidgetAction(&menu);
    act->setDefaultWidget(w);
    menu.addAction(act);
    menu.exec(mapToGlobal(QPoint(0, height())));
}

GSpinBox::GSpinBox(QWidget *parent) : QDoubleSpinBox(parent)
{
    setDecimals(320);  // без округления: значения бывают 1e-30 и 1e30
    setRange(-1e300, 1e300);
    setKeyboardTracking(false);
    setMinimumWidth(40);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

QString GSpinBox::textFromValue(double v) const
{
    if (v == 0)
        return "0";
    return QString::number(v, 'g', 10);
}

double GSpinBox::valueFromText(const QString &t) const
{
    QString s = t.trimmed();
    s.replace(',', '.').replace(QChar(0x2212), '-');
    return s.toDouble();
}

QValidator::State GSpinBox::validate(QString &input, int &) const
{
    QString s = input.trimmed();
    s.replace(',', '.').replace(QChar(0x2212), '-');
    if (s.isEmpty() || s == "-" || s == "+" || s == "." || s.endsWith('e') || s.endsWith('E') || s.endsWith("e-")
        || s.endsWith("E-") || s.endsWith("e+"))
        return QValidator::Intermediate;
    bool ok = false;
    s.toDouble(&ok);
    return ok ? QValidator::Acceptable : QValidator::Invalid;
}

Section::Section(const QString &title, QWidget *parent, bool expanded) : QWidget(parent)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    m_header = new QToolButton(this);
    m_header->setText(title);
    m_header->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_header->setAutoRaise(true);
    m_header->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_header->setObjectName("sectionHeader");
    m_header->setCheckable(true);
    QFont f = m_header->font();
    f.setWeight(QFont::DemiBold);
    m_header->setFont(f);
    lay->addWidget(m_header);
    m_content = new QWidget(this);
    m_body = new QVBoxLayout(m_content);
    m_body->setContentsMargins(10, 2, 8, 10);
    m_body->setSpacing(6);
    m_form = new QFormLayout();
    m_form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_form->setHorizontalSpacing(8);
    m_form->setVerticalSpacing(5);
    m_body->addLayout(m_form);
    lay->addWidget(m_content);
    connect(m_header, &QToolButton::toggled, this, &Section::setExpanded);
    // состояние сворачивания запоминается
    const QString key = "sections/" + title;
    QSettings st;
    setExpanded(st.value(key, expanded).toBool());
    connect(m_header, &QToolButton::toggled, this, [key](bool on) { QSettings().setValue(key, on); });
}

void Section::setExpanded(bool e)
{
    m_expanded = e;
    m_header->blockSignals(true);
    m_header->setChecked(e);
    m_header->blockSignals(false);
    m_header->setArrowType(e ? Qt::DownArrow : Qt::RightArrow);
    m_content->setVisible(e);
}

}  // namespace ng
