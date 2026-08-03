#pragma once

#include <QImage>

#include "model/slide_model.hpp"

// Slide renderer (Feature F1b): a PURE function from a parsed Slide to a pixel
// image. Deterministic and side-effect-free — no file/zip access, no threading
// (the off-thread pre-render orchestration is the presentation controller's job,
// Bible §3 / TM-018). Rendering into a QImage is headless: it needs a
// QGuiApplication for the font database but no display server.
namespace pptv {

class SlideRenderer {
  public:
    // Renders `slide` into a `targetW` x `targetH` QImage. The slide is scaled
    // uniformly to fit (preserving aspect from slideWidthEmu:slideHeightEmu) and
    // centered, with black letterbox bars outside it. A placeholder slide renders
    // a dark "slide unavailable" surface. A non-positive slide size renders a
    // safe neutral image rather than dividing by zero (audit F1a-5).
    static QImage render(const Slide& slide, Emu slideWidthEmu, Emu slideHeightEmu, int targetW,
                         int targetH);

    // Convenience overload taking the whole Presentation's slide size.
    static QImage render(const Presentation& pres, int slideIndex, int targetW, int targetH);
};

} // namespace pptv
