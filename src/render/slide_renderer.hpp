#pragma once

#include <QImage>
#include <QRectF>

#include "model/slide_model.hpp"

// Slide renderer (Feature F1b): a PURE function from a parsed Slide to a pixel
// image. Deterministic and side-effect-free — no file/zip access, no threading
// (the off-thread pre-render orchestration is the presentation controller's job,
// Bible §3 / TM-018). Rendering into a QImage is headless: it needs a
// QGuiApplication for the font database but no display server.
namespace pptv {

// The sub-rectangle of a source image that <a:srcRect> selects, clamped to the
// image. Exposed because the CLAMP is the interesting part: negative insets ask for
// area outside the image (6 of the reference deck's 12 srcRect elements do), and a
// rendered-pixel assertion is too coarse to notice when the clamp is removed —
// measured, a UAT-4 mutation of it survived a colour-based test (BUG-62).
QRectF slideSourceRect(const QSize& imageSize, const SrcRect& sr);

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
