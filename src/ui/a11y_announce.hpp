#pragma once

#include <QString>

class QObject;

// Speaking to a screen reader, in the one way that actually reaches macOS.
//
// This exists because the first accessibility fix was a NO-OP on the only platform
// this ships on (BUG-80). It raised QAccessible::Alert and DescriptionChanged;
// neither has an AppKit equivalent, so Qt's Cocoa plugin discards both and VoiceOver
// says nothing. `nm -mu libqcocoa.dylib` gives the complete set the plugin can post:
// Focused / SelectedText / Title / ValueChanged notifications, plus
// NSAccessibilityAnnouncementRequestedNotification with AnnouncementKey and
// PriorityKey. Only the last carries a message.
//
// It is a shared function rather than two call sites because it also needs a VERSION
// guard, and a guard duplicated is a guard that drifts (BUG-87):
// QAccessibleAnnouncementEvent and QAccessible::Announcement both arrived in Qt 6.8.
// The dev and release platform is macOS with Qt 6.11, but CI builds on Ubuntu's
// packaged Qt6, which is older — so the first version of this did not compile there
// at all. On a Qt without the event this is a no-op, which is honest: there is
// nothing on that platform for it to reach.
namespace pptv {

// True when this build can actually announce. Tests assert against this rather than
// assuming, so the suite states what the platform can do instead of guessing.
bool accessibilityAnnouncementsAvailable();

// Ask the platform to speak `message` on behalf of `about`. Silent no-op when the
// bridge is inactive or the Qt build predates the event.
void announceToScreenReader(QObject* about, const QString& message);

} // namespace pptv
