#pragma once

#include <QRectF>
#include <QSize>
#include <QtGlobal>
#include <vector>

// Pure display geometry for the presentation surface (Feature F7b).
// Everything here is decided without a real screen, so it is unit-testable with
// no monitor attached — which matters because the projector only exists on stage.
namespace pptv {

// A screen described as plain data, so screen-selection and target-sizing logic can
// be tested with zero real monitors.
struct ScreenInfo {
    QSize geometry; // logical pixels
    qreal devicePixelRatio = 1.0;
    bool primary = false;
    QString name;
};

// Where to draw a slide raster inside a widget, preserving aspect and centring the
// remainder (the black letterbox bars). Returns LOGICAL widget coordinates.
// Returns an empty rect for any degenerate input rather than dividing by zero.
QRectF fitRect(const QSize& imageDevicePx, const QSize& widgetLogical, qreal widgetDpr);

// The raster size to render slides at, given the screens we might present on.
// Clamped: never smaller than a usable fallback, never larger than kMaxRenderTarget
// (a 6K screen at dpr 2 would otherwise ask for a 12032x6768 raster per slide).
QSize renderTargetPolicy(const std::vector<ScreenInfo>& screens);

// The raster size to render at, given the screens AND the deck's own aspect ratio.
//
// Prefer this over renderTargetPolicy(). SlideRenderer letterboxes the slide INSIDE
// whatever target it is given, baking black bars into the raster; the surface then
// letterboxes that raster again against the window. If the target aspect does not
// match the deck's, the slide is boxed TWICE and shrinks. Choosing the presentation
// screen (the external display when present) and fitting the DECK's aspect into it
// makes the first letterbox a no-op, so exactly one letterbox happens, at paint time.
QSize renderTargetForDeck(const std::vector<ScreenInfo>& screens, const QSize& deckAspect);

inline constexpr int kFallbackTargetW = 1280;
inline constexpr int kFallbackTargetH = 720;
inline constexpr int kMaxTargetW = 3840;
inline constexpr int kMaxTargetH = 2160;

// What the slide surface should show right now. Holding the last good slide for a
// short grace period stops a brief re-render from flashing the projector black.
enum class SurfaceState {
    Ready,            // the requested slide's raster is available
    HoldLastGood,     // keep showing the previous slide during a short re-render
    RenderingSurface, // show "Rendering slide N..." — never a blank screen
};

SurfaceState surfaceStateFor(bool hasRaster, bool hasLastGood, qint64 msSinceRequest,
                             qint64 graceMs);

} // namespace pptv
