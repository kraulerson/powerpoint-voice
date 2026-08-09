#include <QApplication>
#include <QCommandLineParser>
#include <QFont>
#include <QFontDatabase>

#include "core/app_info.hpp"
#include "ui/app_shell.hpp"
#include "ui/quit_policy.hpp"

int main(int argc, char** argv) {
    // BUG-34 — stop Qt following the desktop theme, BEFORE the application exists.
    //
    // The residual of BUG-30 is that a theme change invalidates Qt's font state on
    // the GUI thread while the pre-render worker is reading it. The BUG-30 fix
    // removed the trigger we control (showing our own window); this removes the
    // class. With desktop-settings awareness off, Qt no longer reacts to macOS
    // switching to dark mode at sunset, a display being attached, or a remote-desktop
    // session reconnecting — the three spontaneous triggers we could not preempt,
    // because the invalidation happens inside Qt before any handler of ours runs.
    //
    // Safe here because this application has no desktop-theme integration to lose: it
    // paints a fixed minimal-dark shell and a black presentation surface (Bible §9),
    // and the slide colours come from the DECK, not the platform palette.
    QApplication::setDesktopSettingsAware(false);
    QApplication app(argc, argv);
    QApplication::setApplicationName(pptv::appName());
    QApplication::setApplicationVersion(pptv::appVersion());

    // ---------------------------------------------------------------------
    // BUG-30 — make the font database safe to touch from the pre-render thread.
    //
    // Slides are rasterised on a worker thread (TM-018 ISOLATE), and rasterising
    // text means Qt's font database. That database is global state the GUI thread
    // also mutates: on every platform theme change, QGuiApplicationPrivate::
    // handleThemeChanged() clears and re-initialises the application font. Karl's
    // crash report (powerpoint_voice-2026-08-04-190008.ips) caught exactly that
    // collision — faulting thread 5 was our QThread inside
    // QFontDatabasePrivate::findFont() -> initFontDef() -> QString::operator=,
    // while the main thread was inside QApplicationPrivate::handleThemeChanged().
    //
    // Two mitigations, both on the GUI thread and both before any worker exists:
    //
    //  1. Set the application font EXPLICITLY. Qt only clears and re-initialises
    //     the application font on a theme change when the application has NOT set
    //     one itself, so claiming it removes that write from the theme-change path.
    //  2. Populate the font database now. Otherwise the first worker-thread text
    //     run triggers lazy population off the GUI thread. Karl's deck names ~50
    //     families (Mulish, Century Gothic, Segoe UI, ...) and almost none exist on
    //     macOS, so nearly every run takes the slow full-fallback path through the
    //     database — which is what made his window of exposure so wide.
    // An application quit must be obeyed even before any presentation window exists
    // (BUG-31). PresentationWindow installs this too; the filter is a singleton.
    pptv::installApplicationQuitFilter();

    QApplication::setFont(QApplication::font());
    (void)QFontDatabase::families();

    // A deck may be passed on the command line so the app can be launched straight
    // into a presentation — useful for a pre-show check without clicking through.
    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Voice-controlled presentation: renders a .pptx and responds to "
                       "spoken and typed commands."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("deck"),
                                 QStringLiteral("Optional .pptx to open on launch."));
    parser.process(app);

    pptv::AppShell shell;
    const QStringList args = parser.positionalArguments();
    if (!args.isEmpty()) {
        shell.openDeck(args.first());
    } else {
        shell.showStart();
    }
    return QApplication::exec();
}
