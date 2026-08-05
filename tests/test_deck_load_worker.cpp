#include <doctest/doctest.h>

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QSemaphore>
#include <QSignalSpy>
#include <QThread>

#include "present/deck_load_worker.hpp"

// NOTE on the pattern `REQUIRE(spy.count() > 0 || spy.wait(N))`:
// QSignalSpy::wait() runs an event loop and counts only what arrives DURING it. A
// worker thread that emits BEFORE the main thread reaches wait() is invisible to it,
// so the wait burns its full timeout and fails while the spy already holds the
// signal. Measured by a UAT-4 tester: 199 of 200 iterations. That made five tests in
// these two files fail ~19% of clean runs and 30% under load (BUG-40) — and I
// re-ran past it three times instead of fixing it. Check the count FIRST.

using namespace pptv;

namespace {

// Drives a worker on a REAL thread and guarantees the thread is torn down, so a
// hung worker fails the test instead of hanging the suite.
struct WorkerHarness {
    QThread thread;
    DeckLoadWorker worker;

    WorkerHarness() {
        registerPresentMetaTypes();
        worker.moveToThread(&thread);
        QObject::connect(&thread, &QThread::started, &worker, &DeckLoadWorker::start);
    }
    ~WorkerHarness() {
        worker.cancel();
        thread.quit();
        REQUIRE(thread.wait(QDeadlineTimer(5000)));
    }
};

Presentation deckOf(int slides) {
    Presentation p;
    p.slideWidth = 9144000;
    p.slideHeight = 6858000;
    p.slides.resize(static_cast<std::size_t>(slides));
    return p;
}

} // namespace

// ===========================================================================
// GROUP P — off-thread deck load. Parsing an untrusted .pptx is unbounded work;
// on the UI thread it looks like a frozen app in front of the room.
// ===========================================================================

TEST_CASE("P: the metatypes needed to cross the thread boundary are registered") {
    CHECK(registerPresentMetaTypes() != 0);
    CHECK(QMetaType::fromName("pptv::DeckLoadOutcome").isValid());
}

TEST_CASE("P: a short content hash identifies a deck without recording its name") {
    // Known SHA-256 of "hello" begins 2cf24dba.
    CHECK(sha256Short(QByteArray("hello")) == QStringLiteral("2cf24dba"));
    const QString empty = sha256Short(QByteArray());
    CHECK(empty.size() == 8);
    CHECK(sha256Short(QByteArray("abc")) == sha256Short(QByteArray("abc")));
    CHECK(sha256Short(QByteArray("abc")) != sha256Short(QByteArray("abd")));
}

TEST_CASE("P: the deck crosses to the GUI thread as a pointer, not a deep copy") {
    WorkerHarness h;
    const Presentation* addr = nullptr;
    h.worker.setLoadFn([&](const QString&) {
        LoadResult r;
        r.ok = true;
        r.presentation = deckOf(40);
        return r;
    });
    QSignalSpy spy(&h.worker, &DeckLoadWorker::loaded);
    h.thread.start();
    REQUIRE((spy.count() > 0 || spy.wait(5000)));
    REQUIRE(spy.count() == 1);
    const auto out = spy.at(0).at(0).value<DeckLoadOutcome>();
    REQUIRE(out.ok);
    REQUIRE(out.presentation);
    CHECK(out.presentation->slides.size() == 40);
    addr = out.presentation.get();
    // A second reference to the same deck must be the SAME object, not a copy.
    const auto again = out.presentation;
    CHECK(again.get() == addr);
}

TEST_CASE("P: loading runs OFF the calling thread") {
    WorkerHarness h;
    QThread* seen = nullptr;
    h.worker.setLoadFn([&](const QString&) {
        seen = QThread::currentThread();
        LoadResult r;
        r.ok = true;
        r.presentation = deckOf(3);
        return r;
    });
    QSignalSpy spy(&h.worker, &DeckLoadWorker::loaded);
    h.thread.start();
    REQUIRE((spy.count() > 0 || spy.wait(5000)));
    CHECK(seen != nullptr);
    CHECK(seen != QThread::currentThread());
}

TEST_CASE("P: a result arriving after cancel is discarded, never delivered late") {
    WorkerHarness h;
    QSemaphore gate;
    h.worker.setLoadFn([&](const QString&) {
        gate.acquire(); // hold the parse open until the test cancels
        LoadResult r;
        r.ok = true;
        r.presentation = deckOf(40);
        return r;
    });
    QSignalSpy loadedSpy(&h.worker, &DeckLoadWorker::loaded);
    QSignalSpy finishedSpy(&h.worker, &DeckLoadWorker::finished);
    h.thread.start();
    QThread::msleep(50); // let the worker enter the parse
    h.worker.cancel();
    gate.release(); // the parse now completes — its result must be dropped
    REQUIRE((finishedSpy.count() > 0 || finishedSpy.wait(5000)));
    CHECK(loadedSpy.count() == 0);
}

TEST_CASE("P: every failure kind survives the thread boundary, typed") {
    const LoadErrorKind kinds[] = {LoadErrorKind::FileNotFound,
                                   LoadErrorKind::NotAZip,
                                   LoadErrorKind::MissingPresentationPart,
                                   LoadErrorKind::MalformedXml,
                                   LoadErrorKind::FileTooLarge,
                                   LoadErrorKind::TooManySlides,
                                   LoadErrorKind::PartTooLarge,
                                   LoadErrorKind::DecompressionLimit};
    for (auto kind : kinds) {
        WorkerHarness h;
        h.worker.setLoadFn([kind](const QString&) {
            LoadResult r;
            r.ok = false;
            r.error = LoadError{kind, QStringLiteral("detail")};
            return r;
        });
        QSignalSpy spy(&h.worker, &DeckLoadWorker::loaded);
        h.thread.start();
        // This case builds and tears down EIGHT worker threads in a loop. The
        // spy-race fix above cut the group failure rate from ~15-30%% to ~2.5%%, but
        // THIS case still fails at that rate and polling to a 15 s deadline did NOT
        // change it — so the residual is neither the spy race nor scheduling delay,
        // and I have not diagnosed it. Recorded honestly rather than claimed fixed
        // (BUG-40 stays open with this measurement). Polling is kept only because a
        // count check is authoritative — QSignalSpy records from the emitting thread
        // — not because it helped.
        QDeadlineTimer deadline(15000);
        while (spy.count() == 0 && !deadline.hasExpired()) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        }
        REQUIRE(spy.count() == 1);
        const auto out = spy.at(0).at(0).value<DeckLoadOutcome>();
        CHECK_FALSE(out.ok);
        CHECK(out.error.kind == kind);
        CHECK_FALSE(out.presentation); // a failed load must carry no deck
    }
}

TEST_CASE("P: a worker that is cancelled before it starts does no work at all") {
    WorkerHarness h;
    bool called = false;
    h.worker.setLoadFn([&](const QString&) {
        called = true;
        LoadResult r;
        r.ok = true;
        r.presentation = deckOf(1);
        return r;
    });
    QSignalSpy loadedSpy(&h.worker, &DeckLoadWorker::loaded);
    QSignalSpy finishedSpy(&h.worker, &DeckLoadWorker::finished);
    h.worker.cancel();
    h.thread.start();
    REQUIRE((finishedSpy.count() > 0 || finishedSpy.wait(5000)));
    CHECK(loadedSpy.count() == 0);
    CHECK_FALSE(called);
}
