#include "ui/a11y_announce.hpp"

#include <QtGlobal>

#include <QAccessible>
#include <QAccessibleEvent>

namespace pptv {

// QAccessibleAnnouncementEvent and QAccessible::Announcement both arrived in Qt 6.8.
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
#define PPTV_HAVE_A11Y_ANNOUNCEMENT 1
#else
#define PPTV_HAVE_A11Y_ANNOUNCEMENT 0
#endif

bool accessibilityAnnouncementsAvailable() {
    return PPTV_HAVE_A11Y_ANNOUNCEMENT != 0;
}

void announceToScreenReader(QObject* about, const QString& message) {
    if (about == nullptr || message.isEmpty()) {
        return;
    }
#if PPTV_HAVE_A11Y_ANNOUNCEMENT
    QAccessibleAnnouncementEvent ev(about, message);
    QAccessible::updateAccessibility(&ev);
#else
    // Deliberately nothing. The alternatives on an older Qt are events this
    // platform's bridge discards, and raising one would put the "we announce" claim
    // back into the code while changing nothing a user can hear — which is exactly
    // the defect BUG-80 was. The accessible NAME and DESCRIPTION are still set by
    // the callers, so a screen reader that navigates to the element still reads it.
    Q_UNUSED(about);
    Q_UNUSED(message);
#endif
}

} // namespace pptv
