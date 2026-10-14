// Векторные иконки интерфейса, окрашиваемые под светлую/тёмную тему.
#pragma once

#include <QColor>
#include <QIcon>
#include <QPixmap>
#include <QString>

namespace ng {

QIcon icon(const QString &name);  // цвет текста текущей палитры
QIcon icon(const QString &name, const QColor &color);
QPixmap iconPixmap(const QString &name, const QColor &color, int px, double dpr = 2.0);
void clearIconCache();  // при смене темы

// Образцы для списков: символ, стиль линии, узор заливки, цвет
QIcon symbolIcon(int type, const QColor &c);
QIcon lineStyleIcon(int style, const QColor &c, int w = 48);
QIcon patternIcon(int pattern, const QColor &c);
QIcon colorIcon(const QColor &c);

}  // namespace ng
