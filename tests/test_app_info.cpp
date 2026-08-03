#include <doctest/doctest.h>

#include "core/app_info.hpp"

// Scaffold sanity test: proves the core library links and the test harness runs
// before any feature exists. Feature tests replace/extend this through the
// Build Loop.
TEST_CASE("app identity is populated") {
    CHECK(pptv::appName() == QStringLiteral("powerpoint-voice"));
    CHECK_FALSE(pptv::appVersion().isEmpty());
}
