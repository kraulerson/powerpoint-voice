#include <doctest/doctest.h>

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QEventLoop>
#include <QMetaMethod>
#include <QSignalSpy>
#include <QThread>
#include <atomic>

#include "loader/deck_loader.hpp"
#include "present/pre_render_worker.hpp"
#include "render/slide_renderer.hpp"

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

// ===========================================================================
// GROUP RT — the REAL renderer, on a REAL worker thread.
//
// Why this group exists: every other test in this file injects a fake renderFn,
// so until BUG-30 the production SlideRenderer had NEVER been executed off the GUI
// thread by any test. Karl's real deck then SEGFAULTED on launch — the renderer
// called QImageReader::format(), which PROBES Qt's image plugins to identify an
// unrecognised format, and that plugin load crashes when it happens on a worker
// thread. Five agent-testers and 183 green tests missed it because the seam
// between the two was the one thing nothing exercised.
//
// These tests are deliberately thin on assertions: their value is that they RUN
// the real thing in the real place. A crash here is the finding.
// ===========================================================================
namespace {

// Loads a fixture deck and pre-renders EVERY slide through the production
// SlideRenderer on a worker thread, returning the rasters the worker emitted.
struct RealRenderRun {
    int emitted = 0;
    int placeholders = 0;
    int nullImages = 0;
    bool finished = false;
};

RealRenderRun renderFixtureOffThread(const char* name, const QSize& target = QSize(640, 360)) {
    const QString path = QString::fromUtf8(FIXTURES_DIR) + QLatin1Char('/') + QLatin1String(name);
    LoadResult lr = DeckLoader::load(path);
    REQUIRE(lr.ok);

    auto deck = std::make_shared<const Presentation>(lr.presentation);
    const Emu slideW = deck->slideWidth;
    const Emu slideH = deck->slideHeight;

    QThread thread;
    PreRenderWorker worker;
    worker.setDeck(deck);
    worker.setTarget(target);
    // The PRODUCTION render function, wired exactly as AppShell wires it.
    worker.setRenderFn([slideW, slideH](const Slide& s, const QSize& sz) {
        return SlideRenderer::render(s, slideW, slideH, sz.width(), sz.height());
    });
    worker.setPlaceholderFn([](int, const QSize& sz) {
        QImage img(sz, QImage::Format_RGB32);
        img.fill(Qt::black);
        return img;
    });

    RealRenderRun run;
    // The receiver context MUST be an object living on THIS thread, not the worker.
    // With `&worker` as context the lambdas run on the worker thread and race this
    // thread's polling loop below — ThreadSanitizer caught exactly that in the first
    // version of this harness. A main-thread context makes the connections queued,
    // so the counters are only ever touched here, which is also how AppShell (itself
    // a main-thread QObject) receives these same signals.
    QObject sink;
    QObject::connect(&worker, &PreRenderWorker::slideReady, &sink,
                     [&run](int, const QImage& img, bool isPlaceholder) {
                         ++run.emitted;
                         if (isPlaceholder) {
                             ++run.placeholders;
                         }
                         if (img.isNull()) {
                             ++run.nullImages;
                         }
                     });
    QObject::connect(&worker, &PreRenderWorker::finished, &sink, [&run]() { run.finished = true; });

    worker.moveToThread(&thread);
    QObject::connect(&thread, &QThread::started, &worker, &PreRenderWorker::start);
    thread.start();

    // Pump the GUI thread's event loop so the queued signals are delivered here,
    // then insist the worker terminates within a deadline rather than hanging CI.
    QDeadlineTimer deadline(20000);
    while (!run.finished && !deadline.hasExpired()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    worker.cancel();
    thread.quit();
    REQUIRE(thread.wait(QDeadlineTimer(5000)));
    return run;
}

} // namespace

TEST_CASE("RT1: a text deck pre-renders through the production renderer off-thread") {
    RealRenderRun run = renderFixtureOffThread("good_text.pptx");
    CHECK(run.finished);
    CHECK(run.emitted == 2);
    CHECK(run.nullImages == 0);
    CHECK(run.placeholders == 0);
}

TEST_CASE("RT2: a deck with a PNG pre-renders through the production renderer off-thread") {
    RealRenderRun run = renderFixtureOffThread("good_image.pptx");
    CHECK(run.finished);
    CHECK(run.emitted == 1);
    CHECK(run.nullImages == 0);
}

// BUG-30 regression. The fixture's picture part carries EMF bytes — a format NO
// Qt image plugin handles. That distinction is the whole bug: identifying a GIF is
// cheap because Qt HAS a gif plugin, but EMF matches nothing, so
// QImageReader::format() enumerates and dlopen()s every installed image plugin
// hunting for a handler. That sweep, on the pre-render WORKER thread, is what
// killed the app on Karl's deck (5 EMF parts). The renderer must now see the bytes
// are not allow-listed and substitute a placeholder WITHOUT constructing any Qt
// decoder. If this test crashes rather than fails, that IS the bug.
//
// Verified to catch it: with isAllowedImageFormat() reverted, this test aborts.
TEST_CASE("RT3/BUG-30: EMF bytes do not crash the pre-render worker thread") {
    RealRenderRun run = renderFixtureOffThread("good_emf_image.pptx");
    CHECK(run.finished);
    CHECK(run.emitted == 1);
    CHECK(run.nullImages == 0);
}

TEST_CASE("RT3b/BUG-30: GIF bytes likewise reach no decoder on the worker thread") {
    RealRenderRun run = renderFixtureOffThread("good_gif_image.pptx");
    CHECK(run.finished);
    CHECK(run.emitted == 1);
    CHECK(run.nullImages == 0);
}

// The inherited backgrounds of BUG-32 have to survive the same trip: the loader
// resolves them, and it is the worker thread that paints them.
TEST_CASE("RT4: a deck whose backgrounds are inherited pre-renders off-thread") {
    RealRenderRun run = renderFixtureOffThread("good_bg_inherit.pptx");
    CHECK(run.finished);
    CHECK(run.emitted == 4);
    CHECK(run.nullImages == 0);
}
