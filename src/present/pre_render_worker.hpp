#pragma once

#include <atomic>
#include <functional>

#include <QImage>
#include <QObject>
#include <QSize>
#include <vector>

#include "present/deck_load_worker.hpp"

// Off-thread slide pre-rendering (Feature F7b) — the TM-018 control.
//
// Slides are rasterised on a worker thread at load, NOT lazily during the talk, so a
// legal-but-pathological slide can never stall the UI thread mid-presentation. Per
// the ratified amendment (Bible section 3 A3-1 / TM-018.3-A) the mandate is met by
// three mechanisms:
//   PREVENT  — measure complexity BEFORE painting; over-cap slides become a
//              placeholder without ever entering the renderer (this is what actually
//              stops the render bomb, and it is pure and unit-testable).
//   CONTAIN  — a budget fed measured elapsed time degrades subsequent slides.
//   ISOLATE  — rendering is off-thread, so even a slow slide only delays itself.
namespace pptv {

// Per-slide complexity, measured from the model without painting anything.
struct SlideComplexity {
    int textRuns = 0;
    int shapes = 0;
};

// TM-018 mitigation 2 caps. F7b enforces the two that are countable from the slide
// model today; the image-pixel and per-deck font caps land with F7c.
struct RenderCaps {
    int maxTextRunsPerSlide = 5000;
    int maxShapesPerSlide = 2000;
};

SlideComplexity measureComplexity(const Slide& slide);
bool exceedsCaps(const SlideComplexity& c, const RenderCaps& caps);

// The order to render in: the slide being shown first, then outward, so the
// presenter can start immediately instead of waiting for slide 1..N in sequence.
std::vector<int> renderOrder(int current, int count);

class PreRenderWorker : public QObject {
    Q_OBJECT

  public:
    // Injectable so tests can prove the render bomb is never painted, without
    // needing a real pathological deck or a real renderer.
    using RenderFn = std::function<QImage(const Slide&, const QSize&)>;
    using PlaceholderFn = std::function<QImage(int slideIndex, const QSize&)>;

    explicit PreRenderWorker(QObject* parent = nullptr);

    void setDeck(PresentationPtr deck) { deck_ = std::move(deck); }
    void setTarget(const QSize& target) { target_ = target; }
    void setRenderFn(RenderFn fn) { renderFn_ = std::move(fn); }
    void setPlaceholderFn(PlaceholderFn fn) { placeholderFn_ = std::move(fn); }
    void setCaps(const RenderCaps& caps) { caps_ = caps; }

  public slots:
    void start();
    // Re-steer: the presenter jumped, so render what they are LOOKING AT next.
    void setCurrentIndex(int index);
    void cancel();

  signals:
    // Carries QImage, never QPixmap: QPixmap may only be touched on the GUI thread.
    void slideReady(int index, QImage image, bool isPlaceholder);
    void progress(int done, int total, qint64 elapsedMs);
    void finished();

  private:
    void renderOne(int index);

    PresentationPtr deck_;
    QSize target_{1280, 720};
    RenderFn renderFn_;
    PlaceholderFn placeholderFn_;
    RenderCaps caps_;
    std::atomic<bool> cancelled_{false};
    // start() calls processEvents() between slides, so a queued start() could
    // re-enter it and reset done_/emitted_ mid-run (audit L3). Not reachable today,
    // but ReRenderDeck is an obvious future caller.
    bool running_ = false;
    std::atomic<int> current_{0};
    std::vector<bool> done_;
    int emitted_ = 0;
};

} // namespace pptv
