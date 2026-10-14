#include "export.h"
#include "i18n.h"

#include "render.h"

#include <QFileInfo>
#include <QImageWriter>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QPrinter>
#include <QSvgGenerator>

namespace ng {

QImage renderImage(const Project &pr, double dpi, bool transparent)
{
    const double k = dpi / 72.0;
    const int w = qMax(1, int(std::lround(pr.pageW * k)));
    const int h = qMax(1, int(std::lround(pr.pageH * k)));
    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    const int dpm = int(std::lround(dpi / 0.0254));
    img.setDotsPerMeterX(dpm);
    img.setDotsPerMeterY(dpm);
    img.fill(transparent ? Qt::transparent : Qt::white);
    QPainter p(&img);
    RenderOptions opt;
    opt.drawBackground = !transparent;
    renderPage(&p, pr, QRectF(0, 0, w, h), nullptr, opt);
    p.end();
    return img;
}

bool exportFile(const Project &pr, const QString &path, double dpi, bool transparent, QString *error)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    if (ext == "pdf") {
        QPdfWriter pdf(path);
        pdf.setCreator("NeoGrace");
        pdf.setTitle(QFileInfo(pr.filename).completeBaseName());
        pdf.setResolution(1200);
        pdf.setPageSize(QPageSize(QSizeF(pr.pageW, pr.pageH), QPageSize::Point, QString(), QPageSize::ExactMatch));
        pdf.setPageMargins(QMarginsF(0, 0, 0, 0), QPageLayout::Point);
        QPainter p;
        if (!p.begin(&pdf)) {
            if (error)
                *error = T("Не удалось создать PDF");
            return false;
        }
        const QRect r = pdf.pageLayout().fullRectPixels(pdf.resolution());
        RenderOptions opt;
        opt.drawBackground = !transparent;
        renderPage(&p, pr, QRectF(0, 0, r.width(), r.height()), nullptr, opt);
        p.end();
        return true;
    }
    if (ext == "svg") {
        QSvgGenerator svg;
        svg.setFileName(path);
        svg.setSize(QSize(pr.pageW, pr.pageH));
        svg.setViewBox(QRectF(0, 0, pr.pageW, pr.pageH));
        svg.setResolution(72);
        svg.setTitle(QFileInfo(pr.filename).completeBaseName());
        svg.setDescription("NeoGrace");
        QPainter p;
        if (!p.begin(&svg)) {
            if (error)
                *error = T("Не удалось создать SVG");
            return false;
        }
        RenderOptions opt;
        opt.drawBackground = !transparent;
        renderPage(&p, pr, QRectF(0, 0, pr.pageW, pr.pageH), nullptr, opt);
        p.end();
        return true;
    }
    const bool alpha = transparent && (ext == "png" || ext == "tif" || ext == "tiff");
    QImage img = renderImage(pr, dpi, alpha);
    if (!alpha)
        img = img.convertToFormat(QImage::Format_RGB32);
    QImageWriter w(path);
    if (ext == "jpg" || ext == "jpeg")
        w.setQuality(95);
    if (!w.write(img)) {
        if (error)
            *error = w.errorString();
        return false;
    }
    return true;
}

void printProject(const Project &pr, QPrinter *printer)
{
    QPainter p(printer);
    const QRectF page = printer->pageLayout().paintRectPixels(printer->resolution());
    // вписать с сохранением пропорций
    const double k = qMin(page.width() / pr.pageW, page.height() / pr.pageH);
    const QSizeF sz(pr.pageW * k, pr.pageH * k);
    const QRectF target(QPointF((page.width() - sz.width()) / 2, (page.height() - sz.height()) / 2), sz);
    renderPage(&p, pr, target);
}

}  // namespace ng
