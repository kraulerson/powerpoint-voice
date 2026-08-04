#include "ui/notice_strip.hpp"

#include <QFontMetrics>
#include <QPaintEvent>
#include <QPainter>
#include <algorithm>

namespace pptv {

NoticeStrip::NoticeStrip(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_TransparentForMouseEvents);
}

int NoticeStrip::heightFor(int hostHeight) {
    // BOUNDED by construction: a notice can never grow the strip and push the slide
    // off the projector, however long the message is.
    if (hostHeight <= 0) {
        return kMinHeight;
    }
    return std::clamp(hostHeight / 10, kMinHeight, kMaxHeight);
}

void NoticeStrip::setText(const QString& text) {
    text_ = text;
    update();
}

void NoticeStrip::paintEvent(QPaintEvent*) {
    if (text_.isEmpty()) {
        return;
    }
    QPainter painter(this);
    painter.fillRect(rect(), QColor(0, 0, 0, 180));
    painter.setPen(QColor(235, 235, 235));
    // Elide rather than wrap or grow: a long notice is truncated, and the strip's
    // geometry is untouched.
    const QFontMetrics fm(painter.font());
    const QString shown = fm.elidedText(text_, Qt::ElideRight, rect().width() - 16);
    painter.drawText(rect().adjusted(8, 0, -8, 0), Qt::AlignVCenter | Qt::AlignLeft, shown);
}

} // namespace pptv
