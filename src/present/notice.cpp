#include "present/notice.hpp"

namespace pptv {

QString noticeForRole(const Notice& n, NoticeRole role, bool paused) {
    // Every string below is built from the id plus integers only. There is no
    // parameter through which deck content, a file path, or heard speech could
    // reach a display or a log (Project Bible §8, TM-012/TM-013).
    switch (n.id) {
    case NoticeId::None:
        return QString();
    case NoticeId::CommandEcho:
        // The audience sees only the position; the operator also sees where from.
        return role == NoticeRole::Audience
                   ? QStringLiteral("Slide %1").arg(n.arg)
                   : QStringLiteral("Slide %1 (from %2)").arg(n.arg).arg(n.arg2);
    case NoticeId::DeckHasSlides:
        return QStringLiteral("Deck has %1 slides").arg(n.arg);
    case NoticeId::AlreadyOnSlide:
        return QStringLiteral("Already on slide %1").arg(n.arg);
    case NoticeId::EndOfDeck:
        return QStringLiteral("End of deck — slide %1 of %2").arg(n.arg).arg(n.arg2);
    case NoticeId::AtFirstSlide:
        return QStringLiteral("Already at the first slide");
    case NoticeId::DeckEmpty:
        return QStringLiteral("No deck loaded");
    case NoticeId::Paused:
        // Only meaningful while paused; suppressed once the session resumes.
        return paused ? QStringLiteral("Paused — voice control is off") : QString();
    case NoticeId::Resumed:
        return QStringLiteral("Resumed");
    case NoticeId::SlideNumberTooLong:
        // An operator-only typing hint; never shown to an audience.
        return role == NoticeRole::Operator ? QStringLiteral("Slide number too long") : QString();
    case NoticeId::HoldingHint:
        return QStringLiteral("Presentation paused — press Esc again to exit, any key to resume");
    }
    return QString();
}

} // namespace pptv
