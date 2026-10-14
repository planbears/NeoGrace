// Экспорт страницы: PNG/JPEG/TIFF/BMP, PDF (вектор), SVG (вектор), печать.
#pragma once

#include "model.h"

#include <QImage>
#include <QString>

class QPrinter;

namespace ng {

QImage renderImage(const Project &pr, double dpi = 150, bool transparent = false);
bool exportFile(const Project &pr, const QString &path, double dpi = 300, bool transparent = false,
                QString *error = nullptr);
void printProject(const Project &pr, QPrinter *printer);

}  // namespace ng
