// Чтение и запись файлов Grace (.agr), .xvg и простых таблиц.
//
// Парсер табличный: строки вида "@    s0 symbol size 1.0" сопоставляются со
// схемами из model.h.  Всё, что парсер не понял, сохраняется как есть и
// записывается обратно — файл не теряет настроек, которых NeoGrace не знает.
#pragma once

#include "model.h"

#include <QString>
#include <QVector>

namespace ng {

QString normalizeCommand(const QString &cmd);

// Разбор текста проекта.  keepExtras=false для файлов данных (.xvg, .dat).
Project parseAgr(const QString &text, bool keepExtras = true);

// Загрузка с диска.  Для файлов данных (не .agr) проект оформляется и
// масштабируется автоматически.  Ошибка — пустой filename и текст в error.
bool loadProject(const QString &path, Project &out, QString *error = nullptr);

QString writeAgr(const Project &pr);
bool saveProject(Project &pr, const QString &path, QString *error = nullptr);

// Числовая таблица: пробелы, запятые, табуляции, «;».  Блоки разделяются
// строкой "&" или пустыми строками при blankSplits.
struct Table {
    QVector<QVector<double>> cols;
    QStringList headers;
    int rows() const { return cols.isEmpty() ? 0 : cols[0].size(); }
};
QVector<Table> readTables(const QString &text, bool blankSplits = false);
QString decodeBytes(const QByteArray &raw, QString *encoding = nullptr);
// Строка чисел через пробелы, табуляции, «,» или «;» (не зависит от локали)
bool parseNumberLine(QStringView line, QVector<double> &out);

}  // namespace ng
