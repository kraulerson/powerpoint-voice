#include "core/app_info.hpp"

namespace pptv {

QString appName() {
    return QStringLiteral("powerpoint-voice");
}

QString appVersion() {
    return QStringLiteral(POWERPOINT_VOICE_VERSION);
}

} // namespace pptv
