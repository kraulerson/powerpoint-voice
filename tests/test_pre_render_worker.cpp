#include <doctest/doctest.h>

#include <QDeadlineTimer>
#include <QMetaMethod>
#include <QSignalSpy>
#include <QThread>
#include <atomic>

#include "present/pre_render_worker.hpp"

using namespace pptv;

namespace {

Slide slideWithShapes(int n) {
    Slide s;
    s.elements.resize(static_cast<std::size_t>(n));
    return s;
}

PresentationPtr deckOf(int slides, int shapesEach = 1) {
    Presentation p;
    p.slideWidth = 9144000;
    p.slideHeight = 6858000;
    for (int i = 0; i < slides; ++i) {
        p.slides.push_back(slideWithShapes(shapesEach));
    }
    return std::make_shared<const Presentation>(std::move(p));
}

QImage filled(const QSize& sz) {
    QImage img(sz.isEmpty() ? QSize(1, 1) : sz, QImage::Format_RGB32);
    img.fill(Qt::black);
    return img;
}

// Guarantees the worker thread is torn down within a deadline, so a hung worker
// fails the test rather than hanging the whole suite.
struct RenderHarness {
    QThread thread;
    PreRenderWorker worker;

    RenderHarness() {
        worker.moveToThread(&thread);
        QObject::connect(&thread, &QThread::started, &worker, &PreRenderWorker::start);
        worker.setRenderFn([](const Slide&, const QSize& sz) { return filled(sz); });
        worker.setPlaceholderFn([](int, const QSize& sz) { return filled(sz); });
    }
    ~RenderHarness() {
        worker.cancel();
        thread.quit();
        REQUIRE(thread.wait(QDeadlineTimer(5000)));
    }
};

} // namespace

// ===========================================================================
// GROUP O — off-thread pre-render (TM-018). The UI thread must never be able to
// block on deck content, and a pathological slide must never be painted at all.
// ===========================================================================

TEST_CASE("O: complexity is measured from the model, without painting") {
    Slide s;
    s.elements.resize(3);
    s.elements[0].kind = ElementKind::TextBox;
    s.elements[0].textBox.paragraphs.resize(2);
    s.elements[0].textBox.paragraphs[0].runs.resize(4);
    s.elements[0].textBox.paragraphs[1].runs.resize(5);
    const auto c = measureComplexity(s);
    CHECK(c.shapes == 3);
    CHECK(c.textRuns == 9);
}

TEST_CASE("O: the caps are the TM-018 numbers, and the boundary is exact") {
    RenderCaps caps;
    CHECK(caps.maxShapesPerSlide == 2000);
    CHECK(caps.maxTextRunsPerSlide == 5000);
    CHECK_FALSE(exceedsCaps(SlideComplexity{0, 2000}, caps));
    CHECK(exceedsCaps(SlideComplexity{0, 2001}, caps));
    CHECK_FALSE(exceedsCaps(SlideComplexity{5000, 0}, caps));
    CHECK(exceedsCaps(SlideComplexity{5001, 0}, caps));
}

TEST_CASE("O: rendering starts at the slide being shown, then works outward") {
    const auto order = renderOrder(5, 10);
    REQUIRE(order.size() == 10);
    CHECK(order[0] == 5); // what the presenter is looking at, first
    // every slide exactly once, all in range
    std::vector<int> seen(10, 0);
    for (int i : order) {
        REQUIRE(i >= 0);
        REQUIRE(i < 10);
        seen[static_cast<std::size_t>(i)]++;
    }
    for (int n : seen) {
        CHECK(n == 1);
    }
    // distance from the current slide never decreases
    for (std::size_t i = 1; i < order.size(); ++i) {
        CHECK(qAbs(order[i] - 5) >= qAbs(order[i - 1] - 5));
    }
    CHECK(renderOrder(0, 0).empty());
}

TEST_CASE("O: every slide is rendered exactly once, with the right index") {
    RenderHarness h;
    h.worker.setDeck(deckOf(4));
    h.worker.setTarget(QSize(1280, 720));
    QSignalSpy ready(&h.worker, &PreRenderWorker::slideReady);
    QSignalSpy fin(&h.worker, &PreRenderWorker::finished);
    h.thread.start();
    REQUIRE(fin.wait(5000));
    CHECK(ready.count() == 4);
    std::vector<int> seen(4, 0);
    for (int i = 0; i < ready.count(); ++i) {
        const int idx = ready.at(i).at(0).toInt();
        REQUIRE(idx >= 0);
        REQUIRE(idx < 4);
        seen[static_cast<std::size_t>(idx)]++;
    }
    for (int n : seen) {
        CHECK(n == 1);
    }
}

TEST_CASE("O: rendering PROVABLY happens off the calling thread (TM-018 isolate)") {
    RenderHarness h;
    std::atomic<int> calls{0};
    QThread* const testThread = QThread::currentThread();
    std::atomic<bool> anyOnTestThread{false};
    h.worker.setRenderFn([&](const Slide&, const QSize& sz) {
        ++calls;
        if (QThread::currentThread() == testThread) {
            anyOnTestThread = true;
        }
        return filled(sz);
    });
    h.worker.setDeck(deckOf(5));
    QSignalSpy fin(&h.worker, &PreRenderWorker::finished);
    h.thread.start();
    REQUIRE(fin.wait(5000));
    CHECK(calls.load() == 5);
    CHECK_FALSE(anyOnTestThread.load()); // never on the UI/test thread
}

TEST_CASE("O: THE RENDER BOMB IS NEVER PAINTED — the cap prevents, it does not abort") {
    // Slide 1 is over the shape cap. The renderer must never be entered for it;
    // a placeholder is produced instead. This is the TM-018 PREVENT mechanism.
    Presentation p;
    p.slides.push_back(slideWithShapes(1));
    p.slides.push_back(slideWithShapes(2001)); // over cap
    p.slides.push_back(slideWithShapes(1));
    RenderHarness h;
    h.worker.setDeck(std::make_shared<const Presentation>(std::move(p)));
    std::atomic<int> rendered{0};
    std::atomic<int> placeheld{0};
    std::atomic<bool> renderedTheBomb{false};
    h.worker.setRenderFn([&](const Slide& s, const QSize& sz) {
        ++rendered;
        if (s.elements.size() > 2000) {
            renderedTheBomb = true;
        }
        return filled(sz);
    });
    h.worker.setPlaceholderFn([&](int, const QSize& sz) {
        ++placeheld;
        return filled(sz);
    });
    QSignalSpy ready(&h.worker, &PreRenderWorker::slideReady);
    QSignalSpy fin(&h.worker, &PreRenderWorker::finished);
    h.thread.start();
    REQUIRE(fin.wait(5000));
    CHECK_FALSE(renderedTheBomb.load()); // the payload was never handed to QPainter
    CHECK(rendered.load() == 2);
    CHECK(placeheld.load() == 1);
    CHECK(ready.count() == 3); // the deck still presents, in full
    bool sawPlaceholder = false;
    for (int i = 0; i < ready.count(); ++i) {
        if (ready.at(i).at(2).toBool()) {
            CHECK(ready.at(i).at(0).toInt() == 1);
            sawPlaceholder = true;
        }
    }
    CHECK(sawPlaceholder);
}

TEST_CASE("O: never a null image — a null raster would be a black projector") {
    for (const QSize& target : {QSize(1, 1), QSize(1280, 720), QSize(3840, 2160)}) {
        RenderHarness h;
        h.worker.setDeck(deckOf(4));
        h.worker.setTarget(target);
        QSignalSpy ready(&h.worker, &PreRenderWorker::slideReady);
        QSignalSpy fin(&h.worker, &PreRenderWorker::finished);
        h.thread.start();
        REQUIRE(fin.wait(5000));
        REQUIRE(ready.count() == 4);
        for (int i = 0; i < ready.count(); ++i) {
            const QImage img = ready.at(i).at(1).value<QImage>();
            CHECK_FALSE(img.isNull());
            CHECK(img.width() > 0);
            CHECK(img.height() > 0);
        }
    }
}

TEST_CASE("O: cancelling before the run does no work and still finishes") {
    RenderHarness h;
    h.worker.setDeck(deckOf(20));
    std::atomic<int> calls{0};
    h.worker.setRenderFn([&](const Slide&, const QSize& sz) {
        ++calls;
        return filled(sz);
    });
    QSignalSpy ready(&h.worker, &PreRenderWorker::slideReady);
    QSignalSpy fin(&h.worker, &PreRenderWorker::finished);
    h.worker.cancel();
    h.thread.start();
    REQUIRE(fin.wait(5000));
    CHECK(ready.count() == 0);
    CHECK(calls.load() == 0);
}

TEST_CASE("O: cancelling mid-run stops promptly and always finishes") {
    RenderHarness h;
    h.worker.setDeck(deckOf(200));
    h.worker.setRenderFn([&](const Slide&, const QSize& sz) {
        QThread::msleep(1);
        return filled(sz);
    });
    QSignalSpy ready(&h.worker, &PreRenderWorker::slideReady);
    QSignalSpy fin(&h.worker, &PreRenderWorker::finished);
    h.thread.start();
    QThread::msleep(30);
    h.worker.cancel();
    REQUIRE(fin.wait(5000)); // bounded shutdown: the app can always exit
    CHECK(ready.count() < 200);
}

TEST_CASE("O: a jump mid-render re-steers to what the presenter is now looking at") {
    RenderHarness h;
    h.worker.setDeck(deckOf(100));
    h.worker.setRenderFn([&](const Slide&, const QSize& sz) {
        QThread::msleep(2);
        return filled(sz);
    });
    QSignalSpy ready(&h.worker, &PreRenderWorker::slideReady);
    QSignalSpy fin(&h.worker, &PreRenderWorker::finished);
    h.thread.start();
    QThread::msleep(20);
    QMetaObject::invokeMethod(&h.worker, "setCurrentIndex", Qt::QueuedConnection, Q_ARG(int, 90));
    REQUIRE(fin.wait(10000));
    CHECK(ready.count() == 100); // still complete
    // slide 90 must have been rendered EARLY relative to the original order
    int posOf90 = -1;
    for (int i = 0; i < ready.count(); ++i) {
        if (ready.at(i).at(0).toInt() == 90) {
            posOf90 = i;
            break;
        }
    }
    REQUIRE(posOf90 >= 0);
    CHECK(posOf90 < 80); // without re-steer it would be near the very end
}

TEST_CASE("O: the ready signal carries QImage, never QPixmap") {
    // QPixmap may only be touched on the GUI thread; carrying one from a worker is
    // undefined behaviour. Assert the declared signal type, not just the value.
    const QMetaObject& mo = PreRenderWorker::staticMetaObject;
    int idx = -1;
    for (int i = 0; i < mo.methodCount(); ++i) {
        if (mo.method(i).name() == QByteArray("slideReady")) {
            idx = i;
            break;
        }
    }
    REQUIRE(idx >= 0);
    CHECK(mo.method(idx).parameterType(1) == static_cast<int>(QMetaType::QImage));
}

TEST_CASE("O: progress is reported honestly for every slide") {
    RenderHarness h;
    h.worker.setDeck(deckOf(3));
    QSignalSpy prog(&h.worker, &PreRenderWorker::progress);
    QSignalSpy fin(&h.worker, &PreRenderWorker::finished);
    h.thread.start();
    REQUIRE(fin.wait(5000));
    REQUIRE(prog.count() == 3);
    for (int i = 0; i < prog.count(); ++i) {
        CHECK(prog.at(i).at(0).toInt() == i + 1); // done
        CHECK(prog.at(i).at(1).toInt() == 3);     // total
        CHECK(prog.at(i).at(2).toLongLong() >= 0);
    }
}
