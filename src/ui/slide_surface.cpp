#include "ui/slide_surface.hpp"

#include <QAccessible>
#include <QAccessibleEvent>
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
    // It must be QAccessibleAnnouncementEvent specifically (BUG-80). The first
    // version of this raised QAccessible::DescriptionChanged, which does NOTHING on
    // macOS: there is no such AppKit notification, so Qt's Cocoa plugin drops the
    // event on the floor. `nm -mu libqcocoa.dylib` shows the complete set it can
    // post — Focused/SelectedText/Title/ValueChanged, plus
    // NSAccessibilityAnnouncementRequestedNotification with AnnouncementKey and
    // PriorityKey. Announcement is the only one of those that carries a message,
    // and macOS is the platform this ships on.
    QAccessibleAnnouncementEvent ev(this, what);
    QAccessible::updateAccessibility(&ev);
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
