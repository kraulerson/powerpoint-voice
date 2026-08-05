#pragma once

#include <QString>

// Who is allowed to end the presentation, and how (BUG-31).
//
// The window deliberately refuses close requests so that a stray Esc or a fumbled
// click cannot end a talk in front of an audience. Taken literally that made the
// application impossible to quit AT ALL: on macOS an application quit (Dock ->
// Quit, Activity Monitor -> Quit, Cmd+Q) is delivered by asking every top-level
// window to close, and Qt cancels the shutdown if any window refuses. Qt documents
// this directly for QCoreApplication::quit(): "The request may be ignored if the
// application prevents the quit, for example if one of its windows can't be closed.
// The application can affect this by handling the QEvent::Quit event on the
// application level, or QEvent::Close events for the individual windows."
//
// So the two requests must be told apart:
//   * close THE WINDOW    — might be the presenter fumbling mid-talk. Confirm it.
//   * quit THE APPLICATION — came from outside the app and is already deliberate.
//                            Obey it, from any mode, with no prompt.
namespace pptv {

// Installs (once per process) an application-level filter that turns a quit request
// into a close every window will accept. Safe to call repeatedly; a no-op without a
// QCoreApplication instance.
void installApplicationQuitFilter();

// True only for the duration of an application-level quit. A window must accept a
// close request while this holds, whatever mode it is in.
bool applicationQuitInProgress();

// The quit chord, named as the user must actually type it ON THIS PLATFORM. Qt maps
// the COMMAND key to Qt::ControlModifier on macOS and the physical Control key to
// Qt::MetaModifier, so a prompt saying "Ctrl+Shift+Q" sends a Mac user to press a
// chord that arrives as Meta|Shift and matches nothing.
QString quitConfirmChord();

// The full instruction line for the quit prompt.
QString quitConfirmHint();

} // namespace pptv
