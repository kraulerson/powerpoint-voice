#include "ui/notice_strip.hpp"

#include <QAccessible>
#include <QAccessibleEvent>
#include <QFontMetrics>
#include <QPaintEvent>
#include <QPainter>
#include <algorithm>

namespace pptv {

NoticeStrip::NoticeStrip(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_TransparentForMouseEvents);
    // A11Y-1. Every notice this strip shows is drawn text, which VoiceOver cannot
    // see — so "Deck has 10 slides" and "Paused" were visible to everyone in the room
    // except a presenter using a screen reader.
    setAccessibleName(QStringLiteral("Notice"));
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
    if (text_ == text) {
        return;
    }
    text_ = text;
    setAccessibleDescription(text_);
    // ANNOUNCE rather than expose. A notice is transient — it fades — so a property a
    // screen reader has to be asked for is one the presenter will never hear. Alert
    // is the role for "say this now", and every notice is drawn from the closed
    // vocabulary in notice.hpp, so nothing from the deck can be spoken aloud
    // (Bible section 8, TM-012/013).
    if (!text_.isEmpty()) {
        QAccessibleEvent ev(this, QAccessible::Alert);
        QAccessible::updateAccessibility(&ev);
    }
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
