#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include <QGuiApplication>

// Renderer tests paint text, which needs Qt's font database — so the suite runs
// under a QGuiApplication on the offscreen platform (no display server needed).
int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    doctest::Context context(argc, argv);
    return context.run();
}
