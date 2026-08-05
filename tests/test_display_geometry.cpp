#include <doctest/doctest.h>

#include "present/display_geometry.hpp"

using namespace pptv;

namespace {
bool near(qreal a, qreal b) {
    return qAbs(a - b) < 1e-9;
}
void checkRect(const QRectF& r, qreal x, qreal y, qreal w, qreal h) {
    CHECK(near(r.x(), x));
    CHECK(near(r.y(), y));
    CHECK(near(r.width(), w));
    CHECK(near(r.height(), h));
}
} // namespace

// ===========================================================================
// GROUP L — display geometry. A wrong rect here is a distorted or off-centre
// slide on the projector; a degenerate input must never divide by zero.
// ===========================================================================

TEST_CASE("L: a matching aspect fills the whole widget exactly") {
    checkRect(fitRect(QSize(3840, 2160), QSize(1920, 1080), 2.0), 0, 0, 1920, 1080);
}

TEST_CASE("L: a wider image letterboxes top and bottom, equally") {
    checkRect(fitRect(QSize(1920, 1080), QSize(1024, 768), 1.0), 0, 96, 1024, 576);
}

TEST_CASE("L: a taller image pillarboxes left and right, equally") {
    checkRect(fitRect(QSize(1024, 768), QSize(1920, 1080), 1.0), 240, 0, 1440, 1080);
}

TEST_CASE("L: aspect is preserved and the rect stays inside the widget, always") {
    const QSize images[] = {QSize(1920, 1080), QSize(1024, 768), QSize(3840, 2160),
                            QSize(800, 600)};
    const QSize widgets[] = {QSize(1920, 1080), QSize(1024, 768), QSize(1366, 768)};
    for (const auto& img : images) {
        for (const auto& w : widgets) {
            for (qreal dpr : {1.0, 2.0}) {
                const QRectF r = fitRect(img, w, dpr);
                REQUIRE(r.width() > 0);
                REQUIRE(r.height() > 0);
                // inside the widget
                CHECK(r.x() >= -1e-9);
                CHECK(r.y() >= -1e-9);
                CHECK(r.right() <= w.width() + 1e-9);
                CHECK(r.bottom() <= w.height() + 1e-9);
                // aspect preserved
                const qreal want = static_cast<qreal>(img.width()) / img.height();
                CHECK(qAbs(r.width() / r.height() - want) < 1e-6);
                // centred
                CHECK(near(r.x() * 2 + r.width(), w.width()));
                CHECK(near(r.y() * 2 + r.height(), w.height()));
            }
        }
    }
}

TEST_CASE("L: degenerate inputs return an empty rect, never a divide by zero") {
    CHECK(fitRect(QSize(0, 0), QSize(1920, 1080), 1.0).isEmpty());
    CHECK(fitRect(QSize(1920, 1080), QSize(0, 0), 1.0).isEmpty());
    CHECK(fitRect(QSize(1920, 1080), QSize(1920, 1080), 0.0).isEmpty());
    CHECK(fitRect(QSize(-5, 10), QSize(100, 100), 1.0).isEmpty());
}

TEST_CASE("L: the render target is clamped at both ends") {
    CHECK(renderTargetPolicy({ScreenInfo{QSize(0, 0), 1.0, true, {}}}) ==
          QSize(kFallbackTargetW, kFallbackTargetH));
    CHECK(renderTargetPolicy({}) == QSize(kFallbackTargetW, kFallbackTargetH));
    // an invalid dpr must not zero the target
    CHECK(renderTargetPolicy({ScreenInfo{QSize(1920, 1080), 0.0, true, {}}}) == QSize(1920, 1080));
    // a 6K screen at dpr 2 would ask for 12032x6768 per slide — clamp it
    const QSize big = renderTargetPolicy({ScreenInfo{QSize(6016, 3384), 2.0, true, {}}});
    CHECK(big.width() <= kMaxTargetW);
    CHECK(big.height() <= kMaxTargetH);
    CHECK(big.width() > 0);
    CHECK(big.height() > 0);
}

TEST_CASE("L: a brief re-render holds the last good slide instead of flashing black") {
    CHECK(surfaceStateFor(true, true, 0, 400) == SurfaceState::Ready);
    CHECK(surfaceStateFor(false, true, 399, 400) == SurfaceState::HoldLastGood);
    CHECK(surfaceStateFor(false, true, 400, 400) == SurfaceState::RenderingSurface);
    CHECK(surfaceStateFor(false, false, 0, 400) == SurfaceState::RenderingSurface);
}

// ===========================================================================
// UAT-3 REMEDIATION — renderTargetForDeck. The old policy picked the largest
// screen by DEVICE pixels, which on a Retina laptop + 1080p projector is the
// LAPTOP (3024x1964, aspect 1.54). SlideRenderer bakes letterbox bars into the
// raster at the target aspect, and the surface then boxes that raster AGAIN
// against the 16:9 window — so the deck covered 75% of the projector with 13%
// smaller text, for the whole talk, invisibly in rehearsal.
// ===========================================================================

TEST_CASE("UAT3: the render target matches the DECK's aspect, not the screen's") {
    const QSize deck16x9(9144000, 5143500); // EMU, 16:9
    const std::vector<ScreenInfo> macbookPlusProjector{
        ScreenInfo{QSize(1512, 982), 2.0, true, QStringLiteral("laptop")},
        ScreenInfo{QSize(1920, 1080), 1.0, false, QStringLiteral("projector")}};
    const QSize t = renderTargetForDeck(macbookPlusProjector, deck16x9);
    const qreal deckAspect = 16.0 / 9.0;
    const qreal got = static_cast<qreal>(t.width()) / t.height();
    CHECK(qAbs(got - deckAspect) < 0.01); // no bars are baked in

    // and it is sized for the PROJECTOR (the non-primary screen), not the laptop
    CHECK(t.width() <= 1920);
    CHECK(t.width() >= 1900);
}

TEST_CASE("UAT3: a deck-aspect raster produces ONE letterbox, covering the screen") {
    const QSize deck16x9(9144000, 5143500);
    const std::vector<ScreenInfo> screens{
        ScreenInfo{QSize(1512, 982), 2.0, true, QStringLiteral("laptop")},
        ScreenInfo{QSize(1920, 1080), 1.0, false, QStringLiteral("projector")}};
    const QSize target = renderTargetForDeck(screens, deck16x9);
    // painting that raster into the 1920x1080 projector window must FILL it
    const QRectF r = fitRect(target, QSize(1920, 1080), 1.0);
    CHECK(qAbs(r.width() - 1920.0) < 2.0);
    CHECK(qAbs(r.height() - 1080.0) < 2.0);
    CHECK(r.x() < 1.0); // no side bars
    CHECK(r.y() < 1.0); // no top/bottom bars
}

TEST_CASE("UAT3: renderTargetForDeck degrades safely") {
    CHECK(renderTargetForDeck({}, QSize(16, 9)) == QSize(kFallbackTargetW, kFallbackTargetH));
    const std::vector<ScreenInfo> one{ScreenInfo{QSize(1920, 1080), 1.0, true, {}}};
    CHECK(renderTargetForDeck(one, QSize(0, 0)) == renderTargetPolicy(one));
    const QSize huge =
        renderTargetForDeck({ScreenInfo{QSize(6016, 3384), 2.0, false, {}}}, QSize(16, 9));
    CHECK(huge.width() <= kMaxTargetW);
    CHECK(huge.height() <= kMaxTargetH);
}
