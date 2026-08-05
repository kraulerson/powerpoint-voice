#include "ui/quit_policy.hpp"

#include <QCoreApplication>
#include <QEvent>
#include <QObject>
#include <QWidget>

#ifdef QT_WIDGETS_LIB
#include <QApplication>
#endif

namespace pptv {
namespace {

bool g_quitInProgress = false;

// Watches the application object for QEvent::Quit — the point Qt documents for
// influencing whether a quit succeeds.
//
// It does NOT consume the event. It closes the windows itself, with the
// "application is quitting" flag raised so the presentation window accepts instead
// of refusing, and lowers the flag immediately afterwards. Qt's own handling then
// runs, finds no visible top-level window left, and completes the shutdown.
//
// Scoping the flag to exactly this call matters: it is never left raised, so a
// window created later (or in the next test in the same process) still protects
// itself from an ordinary stray close.
class QuitFilter : public QObject {
  public:
    using QObject::QObject;

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (watched == qApp && event->type() == QEvent::Quit) {
            g_quitInProgress = true;
#ifdef QT_WIDGETS_LIB
            QApplication::closeAllWindows();
#endif
            g_quitInProgress = false;
        }
        return QObject::eventFilter(watched, event);
    }
};

} // namespace

void installApplicationQuitFilter() {
    if (!qApp) {
        return; // nothing to filter yet; the window installs this again when built
    }
    // Parented to the application so it lives exactly as long as it is useful, and
    // guarded so repeated construction of presentation windows installs only one.
    static QuitFilter* filter = nullptr;
    if (filter) {
        return;
    }
    filter = new QuitFilter(qApp);
    qApp->installEventFilter(filter);
}

bool applicationQuitInProgress() {
    return g_quitInProgress;
}

QString quitConfirmChord() {
#ifdef Q_OS_MACOS
    // Qt::ControlModifier IS the Command key here (Qt swaps Ctrl and Cmd on macOS
    // unless AA_MacDontSwapCtrlAndMeta is set, which this app does not set).
    return QStringLiteral("Cmd+Shift+Q");
#else
    return QStringLiteral("Ctrl+Shift+Q");
#endif
}

QString quitConfirmHint() {
    return QStringLiteral("Quit the presentation?  %1 to quit  ·  Esc to go back")
        .arg(quitConfirmChord());
}

} // namespace pptv
