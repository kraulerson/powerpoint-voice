#include "ui/slide_surface.hpp"

#include <QPaintEvent>
#include <QPainter>

#include "ui/a11y_announce.hpp"

namespace pptv {

SlideSurface::SlideSurface(QWidget* parent) : QWidget(parent) {
    setAutoFillBackground(false);
    // Black surround: the letterbox bars must read as deliberate, not as a broken
    // window, and a dark surface is far less distracting on a projector.
    QPalette p = palette();
    p.setColor(QPalette::Window, Qt::black);
    setPalette(p);
    // A11Y-1. Without this the surface is an unnamed rectangle to VoiceOver — the
    // deck, the blackout and the quit prompt are all the same silence.
    setAccessibleName(QStringLiteral("Slide"));
    setAccessibleDescription(QStringLiteral("No deck open."));
}

void SlideSurface::setAccessibleState(const QString& what) {
    if (accessibleDescription() == what) {
        return; // do not make a screen reader repeat itself on every repaint
    }
    // Set the property, so it is there when the user navigates to this element...
    setAccessibleDescription(what);
    // ...and ANNOUNCE it, because a slide change is an event the presenter needs
    // told, not a property they will think to go and read.
    //
    // announceToScreenReader, not a bare QAccessibleEvent (BUG-80/87). The first
    // version raised QAccessible::DescriptionChanged, which does NOTHING on macOS —
    // no such AppKit notification exists, so Qt's Cocoa plugin drops it. The second
    // used the right event but did not compile on CI's older Qt. The helper carries
    // both the correct event and the version guard, in one place.
    announceToScreenReader(this, what);
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
