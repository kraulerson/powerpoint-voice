#pragma once

#include <QString>

// Application identity — a small, GUI-free unit so the core library and its
// test target have something real to compile and exercise from the first
// scaffold commit. Feature logic lands here through the Phase 2 Build Loop.
namespace pptv {

// The product name, as shown in window titles and the About surface.
QString appName();

// Semantic version string, sourced from the CMake project() version.
QString appVersion();

} // namespace pptv
