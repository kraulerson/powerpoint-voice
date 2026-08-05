#include "present/display_geometry.hpp"

namespace pptv {

QRectF fitRect(const QSize& imageDevicePx, const QSize& widgetLogical, qreal widgetDpr) {
    // Reject every degenerate input up front. A zero widget or a zero image would
    // divide by zero; a non-positive dpr means the caller has no real screen yet.
    if (imageDevicePx.width() <= 0 || imageDevicePx.height() <= 0 || widgetLogical.width() <= 0 ||
        widgetLogical.height() <= 0 || widgetDpr <= 0.0) {
        return QRectF();
    }

    // Aspect is dpr-independent (both axes scale together), so it is taken from the
    // raster; the result is in LOGICAL widget coordinates, which is what QPainter
    // wants. Fit whichever axis binds and centre the remainder — those are the black
    // letterbox bars, and they must be equal or the slide looks misaligned on stage.
    const qreal imageAspect = static_cast<qreal>(imageDevicePx.width()) / imageDevicePx.height();
    const qreal widgetAspect = static_cast<qreal>(widgetLogical.width()) / widgetLogical.height();

    qreal w = 0.0;
    qreal h = 0.0;
    if (imageAspect >= widgetAspect) {
        w = widgetLogical.width();
        h = w / imageAspect;
    } else {
        h = widgetLogical.height();
        w = h * imageAspect;
    }
    return QRectF((widgetLogical.width() - w) / 2.0, (widgetLogical.height() - h) / 2.0, w, h);
}

QSize renderTargetPolicy(const std::vector<ScreenInfo>& screens) {
    // Render at the largest device-pixel size we might present at, so a slide is
    // never upscaled on the projector — but bounded, because the raster cost is
    // per slide and a 6K screen at dpr 2 would ask for ~12032x6768 each.
    int bestW = 0;
    int bestH = 0;
    for (const auto& s : screens) {
        if (s.geometry.width() <= 0 || s.geometry.height() <= 0) {
            continue;
        }
        // A non-positive dpr is meaningless; treat it as 1 rather than zeroing the
        // target (which would produce a null raster, i.e. a black projector).
        const qreal dpr = s.devicePixelRatio > 0.0 ? s.devicePixelRatio : 1.0;
        const int w = static_cast<int>(s.geometry.width() * dpr);
        const int h = static_cast<int>(s.geometry.height() * dpr);
        if (static_cast<qint64>(w) * h > static_cast<qint64>(bestW) * bestH) {
            bestW = w;
            bestH = h;
        }
    }
    if (bestW <= 0 || bestH <= 0) {
        return QSize(kFallbackTargetW, kFallbackTargetH);
    }
    // Clamp preserving aspect, so a clamped target still matches the screen shape.
    const qreal scale = qMin(1.0, qMin(static_cast<qreal>(kMaxTargetW) / bestW,
                                       static_cast<qreal>(kMaxTargetH) / bestH));
    return QSize(qMax(1, static_cast<int>(bestW * scale)),
                 qMax(1, static_cast<int>(bestH * scale)));
}

QSize renderTargetForDeck(const std::vector<ScreenInfo>& screens, const QSize& deckAspect) {
    // The screen we will actually present on: the external display when one exists,
    // because renderTargetPolicy's "largest by device pixels" picks the Retina laptop
    // over a 1080p projector — the wrong aspect AND the wrong screen.
    const ScreenInfo* target = nullptr;
    for (const auto& s : screens) {
        if (s.geometry.width() <= 0 || s.geometry.height() <= 0) {
            continue;
        }
        if (!s.primary) {
            target = &s;
            break;
        }
        if (!target) {
            target = &s;
        }
    }
    if (!target || deckAspect.width() <= 0 || deckAspect.height() <= 0) {
        return renderTargetPolicy(screens);
    }
    const qreal dpr = target->devicePixelRatio > 0.0 ? target->devicePixelRatio : 1.0;
    const qreal screenW = target->geometry.width() * dpr;
    const qreal screenH = target->geometry.height() * dpr;
    const qreal aspect = static_cast<qreal>(deckAspect.width()) / deckAspect.height();

    // Fit the deck's aspect inside the screen, then clamp (same bound as above).
    qreal w = screenW;
    qreal h = w / aspect;
    if (h > screenH) {
        h = screenH;
        w = h * aspect;
    }
    const qreal scale = qMin(1.0, qMin(kMaxTargetW / w, kMaxTargetH / h));
    return QSize(qMax(1, static_cast<int>(w * scale)), qMax(1, static_cast<int>(h * scale)));
}

SurfaceState surfaceStateFor(bool hasRaster, bool hasLastGood, qint64 msSinceRequest,
                             qint64 graceMs) {
    if (hasRaster) {
        return SurfaceState::Ready;
    }
    // Briefly keep the previous slide rather than flashing the projector black while
    // a re-render lands. Past the grace period, say so explicitly — an honest
    // "Rendering slide N..." beats a blank screen the presenter cannot interpret.
    if (hasLastGood && msSinceRequest < graceMs) {
        return SurfaceState::HoldLastGood;
    }
    return SurfaceState::RenderingSurface;
}

} // namespace pptv
