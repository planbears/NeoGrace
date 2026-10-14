// Холст страницы: отрисовка, масштаб, выделение и правка мышью.
#pragma once

#include "document.h"
#include "render.h"

#include <QAbstractScrollArea>
#include <QImage>
#include <QTimer>

namespace ng {

class PageView : public QAbstractScrollArea {
    Q_OBJECT
public:
    enum Mode { Select, Zoom, Pan, AddText, AddLine, AddBox, AddEllipse };

    explicit PageView(Document *doc, QWidget *parent = nullptr);

    double zoom() const { return m_zoom; }
    void setZoom(double z, QPoint anchor = QPoint(-1, -1));
    void zoomIn() { setZoom(m_zoom * 1.25); }
    void zoomOut() { setZoom(m_zoom / 1.25); }
    void zoomFit();
    void zoom100() { setZoom(1.0 * logicalDpiX() / 72.0 * 0.75); }
    bool fitMode() const { return m_fit; }

    Mode mode() const { return m_mode; }
    void setMode(Mode m);
    const RenderInfo &info() const { return m_info; }

signals:
    void zoomChanged(double z);
    void cursorInfo(const QString &text);
    void modeChanged(int mode);
    void contextMenuWanted(const ng::Sel &sel, const QPoint &globalPos);
    void editTextWanted(const ng::Sel &sel);
    void openDataWanted(int gid, int sid);
    void propertiesWanted(const ng::Sel &sel);

protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void scrollContentsBy(int dx, int dy) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    bool viewportEvent(QEvent *) override;
    void changeEvent(QEvent *) override;
    void leaveEvent(QEvent *) override;

private:
    void scheduleRender();
    void render();
    void updateScrollBars();
    QRectF pageRect() const;  // страница в координатах окна просмотра
    Sel hitTest(QPointF pos, int *itemIndex = nullptr) const;
    int graphAt(QPointF pos) const;
    int handleAt(QPointF pos) const;  // ручки выделенного графика
    QRectF selectionRect() const;
    QPointF toView(QPointF w) const { return m_info.toView(w); }
    void drawOverlay(QPainter &p);
    void finishCreate(QPointF a, QPointF b);
    Qt::CursorShape cursorFor(QPointF pos) const;

    Document *m_doc;
    double m_zoom = 1.0;
    bool m_fit = true;
    Mode m_mode = Select;
    QImage m_cache;
    RenderInfo m_info;
    QTimer m_renderTimer;
    bool m_dirty = true;

    // перетаскивание
    enum Drag { None, MoveLegend, MoveObject, MoveGraph, ResizeGraph, RubberZoom, PanWorld, Create, ScrollPage };
    Drag m_drag = None;
    QPointF m_press, m_last;
    int m_handle = -1;
    Project m_dragStart;  // состояние на начало перетаскивания
    int m_dragGid = -1;
    Sel m_hover;
    bool m_moved = false;
};

}  // namespace ng
