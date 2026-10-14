// Разметка текста Grace и её отрисовка через QPainter.
//
// Поддерживается: \s \S \N (индексы), \+ \- \z{} \Z{} (размер), \v{} \h{}
// (сдвиги), \x и \f{Symbol} (греческие буквы), \f{имя|номер}, \0–\9, \f{}
// (шрифты), \u \U \o \O (подчёркивание/надчёркивание), \R{цвет}, \#{hex},
// \n (перевод строки), \\ (обратная косая черта).
#pragma once

#include <QColor>
#include <QFont>
#include <QHash>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QString>
#include <QTransform>
#include <QVector>
#include <functional>

class QPainter;
class QPaintDevice;

namespace ng {

struct Project;

struct TextRun {
    QString text;
    int font = 0;
    double scale = 1.0;   // относительный размер
    double vshift = 0.0;  // в долях базового размера, вверх
    double hshift = 0.0;
    bool under = false, over = false;
    int color = -1;  // -1 — цвет по умолчанию
};
using TextLine = QVector<TextRun>;

// fontLookup(имя или номер) → id шрифта
QVector<TextLine> parseGraceText(const QString &s, int baseFont,
                                 const std::function<int(const QString &)> &fontLookup);
QString stripEscapes(const QString &s);
QString unicodeToGrace(const QString &s);  // α → \xa\f{}
QString symbolToUnicode(const QString &s);  // символы шрифта Symbol → Unicode

// Grace-выравнивание
enum Just { JustLeft = 0, JustRight = 1, JustCenter = 2, JustBline = 0,
            JustBottom = 4, JustTop = 8, JustMiddle = 12 };

// Движок вывода текста: шрифты считаются по устройству, чтобы размер в
// пикселях был точным на экране, в PNG, PDF и SVG.
class TextEngine {
public:
    TextEngine(const Project &pr, QPaintDevice *dev);

    QFont font(int fid, double px) const;
    bool isSymbolFont(int fid) const;
    int lookupFont(const QString &name) const;

    // Прямоугольник текста относительно якоря (без поворота), в пикселях;
    // ось Y вниз.
    QRectF bounds(const QString &s, int fid, double px, int just) const;
    // Нарисовать; вернуть повёрнутый контур текста (для выделения мышью).
    QPolygonF draw(QPainter *p, const QString &s, int fid, double px, const QColor &color,
                   QPointF anchor, double angleDeg, int just) const;
    // Контур без рисования
    QPolygonF outline(const QString &s, int fid, double px, QPointF anchor, double angleDeg,
                      int just) const;

private:
    struct Piece {
        QString text;
        QFont font;
        QPointF pos;  // точка базовой линии относительно начала блока
        double width;
        bool under, over;
        int color;
        double px;
    };
    struct Layout {
        QVector<Piece> pieces;
        QRectF box;  // по «чернилам» (вертикаль) и продвижению (горизонталь)
    };
    Layout layout(const QString &s, int fid, double px) const;
    QTransform placement(const QRectF &box, QPointF anchor, double angleDeg, int just) const;

    const Project &m_pr;
    QPaintDevice *m_dev;
    double m_dpi;
    mutable QHash<QString, QFont> m_fontCache;
    mutable QHash<QString, Layout> m_layoutCache;
};

// Семейство Qt по имени шрифта Grace.
struct FontStyle {
    QStringList families;
    bool italic = false;
    bool bold = false;
    bool symbol = false;
};
FontStyle graceFontStyle(const QString &name);

}  // namespace ng
