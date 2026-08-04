#include "ui/slide_surface.hpp"

#include <QPaintEvent>
#include <QPainter>

namespace pptv {

SlideSurface::SlideSurface(QWidget* parent) : QWidget(parent) {
    setAutoFillBackground(false);
    // Black surround: the letterbox bars must read as deliberate, not as a broken
    // window, and a dark surface is far less distracting on a projector.
    QPalette p = palette();
    p.setColor(QPalette::Window, Qt::black);
    setPalette(p);
}

void SlideSurface::setSlideImage(const QImage& img) {
    image_ = img;
    update();
}

void SlideSurface::setStatusText(const QString& text) {
    status_ = text;
    update();
}

void SlideSurface::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);

    if (image_.isNull()) {
        // Never leave a blank window: say what is happening. A presenter can act on
        // "Rendering slide 3..."; they cannot act on a black rectangle.
        lastRect_ = QRectF();
        if (!status_.isEmpty()) {
            painter.setPen(QColor(180, 180, 180));
            painter.drawText(rect(), Qt::AlignCenter, status_);
        }
        return;
    }

    // Where to draw is decided by the PURE fitRect(), so the geometry is unit-tested
    // without a window and this widget holds no policy of its own.
    lastRect_ = fitRect(image_.size(), size(), devicePixelRatioF());
    if (lastRect_.isEmpty()) {
        return;
    }
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage(lastRect_, image_);
}

} // namespace pptv
