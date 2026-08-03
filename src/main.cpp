#include <QApplication>

#include "core/app_info.hpp"
#include "ui/start_view.hpp"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(pptv::appName());
    QApplication::setApplicationVersion(pptv::appVersion());

    pptv::StartView window;
    window.show();

    return QApplication::exec();
}
