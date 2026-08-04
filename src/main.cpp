#include <QApplication>
#include <QCommandLineParser>

#include "core/app_info.hpp"
#include "ui/app_shell.hpp"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(pptv::appName());
    QApplication::setApplicationVersion(pptv::appVersion());

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
