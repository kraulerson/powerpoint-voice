#include "present/pre_render_worker.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <algorithm>

namespace pptv {

SlideComplexity measureComplexity(const Slide& slide) {
    // Measured from the MODEL — no painting, microseconds. This is the whole point
    // of the PREVENT mechanism: we learn a slide is pathological before QPainter
    // ever sees it, rather than trying (and failing) to abort a paint in flight.
    SlideComplexity c;
    c.shapes = static_cast<int>(slide.elements.size());
    for (const auto& e : slide.elements) {
        if (e.kind != ElementKind::TextBox) {
            continue;
        }
        for (const auto& para : e.textBox.paragraphs) {
            c.textRuns += static_cast<int>(para.runs.size());
        }
    }
    return c;
}

bool exceedsCaps(const SlideComplexity& c, const RenderCaps& caps) {
    return c.shapes > caps.maxShapesPerSlide || c.textRuns > caps.maxTextRunsPerSlide;
}

std::vector<int> renderOrder(int current, int count) {
    // Render what the presenter is LOOKING AT first, then outward, so presenting can
    // begin immediately instead of after slide 1..N have all been rasterised.
    std::vector<int> order;
    if (count <= 0) {
        return order;
    }
    const int start = std::clamp(current, 0, count - 1);
    order.push_back(start);
    for (int d = 1; static_cast<int>(order.size()) < count; ++d) {
        if (start + d < count) {
            order.push_back(start + d);
        }
        if (start - d >= 0) {
            order.push_back(start - d);
        }
    }
    return order;
}

PreRenderWorker::PreRenderWorker(QObject* parent) : QObject(parent) {}

void PreRenderWorker::cancel() {
    cancelled_.store(true, std::memory_order_relaxed);
}

void PreRenderWorker::setCurrentIndex(int index) {
    // Delivered by a queued connection, so this runs ON the worker thread between
    // slides — which is what makes a mid-render jump re-steerable at all.
    current_.store(index, std::memory_order_relaxed);
}

void PreRenderWorker::renderOne(int index) {
    if (!deck_ || index < 0 || index >= static_cast<int>(deck_->slides.size())) {
        return;
    }
    const Slide& slide = deck_->slides[static_cast<std::size_t>(index)];

    QElapsedTimer t;
    t.start();
    QImage img;
    bool placeholder = false;

    // PREVENT: over-cap slides never reach the renderer.
    if (exceedsCaps(measureComplexity(slide), caps_)) {
        placeholder = true;
        if (placeholderFn_) {
            img = placeholderFn_(index, target_);
        }
    } else if (renderFn_) {
        img = renderFn_(slide, target_);
    }

    // A null raster would be a BLACK PROJECTOR. Never emit one: fall back to the
    // placeholder, and finally to a 1x1 so the surface always has something valid.
    if (img.isNull()) {
        if (placeholderFn_) {
            img = placeholderFn_(index, target_);
            placeholder = true;
        }
        if (img.isNull()) {
            img = QImage(1, 1, QImage::Format_RGB32);
            img.fill(Qt::black);
            placeholder = true;
        }
    }

    ++emitted_;
    emit slideReady(index, img, placeholder);
    emit progress(emitted_, static_cast<int>(deck_->slides.size()), t.elapsed());
}

void PreRenderWorker::start() {
    if (running_) {
        return; // re-entrancy guard (audit L3): never restart a run in progress
    }
    if (cancelled_.load(std::memory_order_relaxed) || !deck_) {
        emit finished();
        return;
    }
    running_ = true;

    const int count = static_cast<int>(deck_->slides.size());
    done_.assign(static_cast<std::size_t>(count), false);
    emitted_ = 0;

    // Re-plan after every slide rather than walking one fixed list: that is what
    // lets a mid-render jump take effect on the very next slide, and it keeps the
    // total bounded because a slide is only ever rendered once (done_).
    int remaining = count;
    while (remaining > 0) {
        if (cancelled_.load(std::memory_order_relaxed)) {
            break;
        }
        int next = -1;
        for (int idx : renderOrder(current_.load(std::memory_order_relaxed), count)) {
            if (!done_[static_cast<std::size_t>(idx)]) {
                next = idx;
                break;
            }
        }
        if (next < 0) {
            break;
        }
        done_[static_cast<std::size_t>(next)] = true;
        --remaining;
        renderOne(next);

        // Let queued setCurrentIndex() calls land between slides.
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    }
    running_ = false;
    emit finished();
}

} // namespace pptv
