#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>
#include <vector>

#include <QCoreApplication>
#include <QEventLoop>
#include <QSignalSpy>

#include "audio/voice_pipeline.hpp"

using namespace pptv;

// ===========================================================================
// GROUP VP — the join between microphone, converter and decoder.
//
// Both ends are injected, so the threading, conversion and shutdown rules are
// testable on a machine with neither a microphone nor a model. The join is where
// this project's defects have consistently lived: every Critical in the F7b audit
// was in wiring, and BUG-60 existed because AppShell's wiring had no tests at all.
// ===========================================================================

namespace {
class FakeCapture : public IAudioCapture {
  public:
    AudioFormat format{48000, 2};
    CaptureError startResult = CaptureError::None;

    CaptureError start() override {
        if (startResult != CaptureError::None) {
            return startResult;
        }
        running_ = true;
        return CaptureError::None;
    }
    void stop() override { running_ = false; }
    bool isRunning() const override { return running_; }
    AudioFormat deviceFormat() const override { return format; }
    void setSink(CaptureSink s) override { sink_ = std::move(s); }

    void deliverOffThread(const std::vector<std::int16_t>& buf) {
        std::thread t([this, buf] {
            if (sink_) {
                sink_(buf.data(), buf.size());
            }
        });
        t.join();
    }

  private:
    CaptureSink sink_;
    std::atomic<bool> running_{false};
};

// A capture that behaves like a REAL device rather than a passive stub: it drives
// the sink from its own thread, and its stop() does not return until that thread has
// left the sink. That is the contract MiniaudioCapture::stop() provides — via the
// sink mutex the audio callback holds across its whole call, NOT via
// ma_device_stop(), which does not join anything (BUG-79). Modelling it is the only
// way to test the shutdown ordering without a microphone.
class ThreadedFakeCapture : public IAudioCapture {
  public:
    ~ThreadedFakeCapture() override { stop(); }

    CaptureError start() override {
        if (running_.exchange(true)) {
            return CaptureError::None;
        }
        thread_ = std::thread([this] {
            const std::vector<std::int16_t> buf(960 * 2, 0);
            while (!quit_.load()) {
                if (sink_) {
                    sink_(buf.data(), buf.size());
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
        return CaptureError::None;
    }

    void stop() override {
        quit_ = true;
        if (thread_.joinable()) {
            thread_.join(); // the JOIN — this is the property under test
        }
        running_ = false;
    }

    bool isRunning() const override { return running_.load(); }
    AudioFormat deviceFormat() const override { return AudioFormat{48000, 2}; }
    void setSink(CaptureSink s) override { sink_ = std::move(s); }

  private:
    CaptureSink sink_;
    std::atomic<bool> running_{false};
    std::atomic<bool> quit_{false};
    std::thread thread_;
};

void pump(int ms = 200) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, ms);
}
} // namespace

TEST_CASE("VP: a recognised phrase crosses to the GUI thread") {
    VoicePipeline p;
    auto cap = std::make_unique<FakeCapture>();
    FakeCapture* raw = cap.get();
    p.setCapture(std::move(cap));

    std::atomic<bool> decodedOffGui{false};
    const std::thread::id gui = std::this_thread::get_id();
    p.setDecoder([&](const std::int16_t*, std::size_t) {
        if (std::this_thread::get_id() != gui) {
            decodedOffGui = true;
        }
        return QStringLiteral("next slide");
    });

    QSignalSpy heard(&p, &VoicePipeline::phraseHeard);
    REQUIRE(p.start() == CaptureError::None);
    raw->deliverOffThread(std::vector<std::int16_t>(960 * 2, 0));
    pump();

    CHECK(decodedOffGui.load()); // decoding did NOT happen on the GUI thread
    REQUIRE(heard.count() == 1); // ...but the phrase arrived on it
    CHECK(heard.at(0).at(0).toString() == QStringLiteral("next slide"));
}

TEST_CASE("VP: samples reach the decoder already converted to 16 kHz mono") {
    VoicePipeline p;
    auto cap = std::make_unique<FakeCapture>();
    cap->format = AudioFormat{48000, 2};
    FakeCapture* raw = cap.get();
    p.setCapture(std::move(cap));

    std::atomic<std::size_t> got{0};
    p.setDecoder([&](const std::int16_t*, std::size_t n) {
        got = n;
        return QString();
    });
    REQUIRE(p.start() == CaptureError::None);
    raw->deliverOffThread(std::vector<std::int16_t>(960 * 2, 0)); // 960 stereo frames
    CHECK(got.load() == 320);                                     // -> 320 mono @16k
}

TEST_CASE("VP: an empty decode emits nothing — silence is not a command") {
    VoicePipeline p;
    auto cap = std::make_unique<FakeCapture>();
    FakeCapture* raw = cap.get();
    p.setCapture(std::move(cap));
    p.setDecoder([](const std::int16_t*, std::size_t) { return QString(); });

    QSignalSpy heard(&p, &VoicePipeline::phraseHeard);
    REQUIRE(p.start() == CaptureError::None);
    raw->deliverOffThread(std::vector<std::int16_t>(1920, 0));
    pump();
    CHECK(heard.count() == 0);
}

TEST_CASE("VP: after stop(), late samples produce nothing") {
    VoicePipeline p;
    auto cap = std::make_unique<FakeCapture>();
    FakeCapture* raw = cap.get();
    p.setCapture(std::move(cap));
    std::atomic<int> decodes{0};
    p.setDecoder([&](const std::int16_t*, std::size_t) {
        ++decodes;
        return QStringLiteral("next slide");
    });

    QSignalSpy heard(&p, &VoicePipeline::phraseHeard);
    REQUIRE(p.start() == CaptureError::None);
    p.stop();
    raw->deliverOffThread(std::vector<std::int16_t>(1920, 0));
    pump();
    CHECK(decodes.load() == 0);
    CHECK(heard.count() == 0);
}

TEST_CASE("VP: a capture failure is reported and leaves the pipeline stopped") {
    VoicePipeline p;
    auto cap = std::make_unique<FakeCapture>();
    cap->startResult = CaptureError::PermissionDenied;
    p.setCapture(std::move(cap));
    p.setDecoder([](const std::int16_t*, std::size_t) { return QString(); });
    CHECK(p.start() == CaptureError::PermissionDenied);
    CHECK_FALSE(p.isRunning());
    CHECK(captureErrorIsRecoverable(CaptureError::PermissionDenied)); // talk continues
}

TEST_CASE("VP: with no capture or no decoder, start fails rather than half-running") {
    VoicePipeline a;
    a.setDecoder([](const std::int16_t*, std::size_t) { return QString(); });
    CHECK(a.start() == CaptureError::NoDevice);
    CHECK_FALSE(a.isRunning());

    VoicePipeline b;
    b.setCapture(std::make_unique<FakeCapture>());
    CHECK(b.start() == CaptureError::NoDevice);
    CHECK_FALSE(b.isRunning());
}

TEST_CASE("VP: the frame count is DERIVED from the real buffer, so it cannot over-read") {
    // The pipeline computes frames = count / channels rather than trusting a
    // separate frame count, which makes the BUG-56 over-read structurally
    // impossible here: the implied length can never exceed what was supplied.
    VoicePipeline p;
    auto cap = std::make_unique<FakeCapture>();
    cap->format = AudioFormat{48000, 8};
    FakeCapture* raw = cap.get();
    p.setCapture(std::move(cap));
    std::atomic<std::size_t> got{0};
    std::atomic<int> decodes{0};
    p.setDecoder([&](const std::int16_t*, std::size_t n) {
        got = n;
        ++decodes;
        return QString();
    });
    REQUIRE(p.start() == CaptureError::None);

    SUBCASE("a trailing partial frame is dropped, not read into") {
        // 100 samples at 8 channels is 12 whole frames; the odd 4 are discarded.
        raw->deliverOffThread(std::vector<std::int16_t>(100, 0));
        CHECK(decodes.load() == 1);
        // 12 frames @48k downsamples to 4 @16k.
        CHECK(got.load() == 4);
    }

    SUBCASE("fewer samples than one whole frame decodes nothing") {
        raw->deliverOffThread(std::vector<std::int16_t>(5, 0));
        CHECK(decodes.load() == 0);
    }
}

TEST_CASE("VP: an invalid device format decodes nothing rather than guessing") {
    VoicePipeline p;
    auto cap = std::make_unique<FakeCapture>();
    cap->format = AudioFormat{0, 0};
    FakeCapture* raw = cap.get();
    p.setCapture(std::move(cap));
    std::atomic<int> decodes{0};
    p.setDecoder([&](const std::int16_t*, std::size_t) {
        ++decodes;
        return QString();
    });
    REQUIRE(p.start() == CaptureError::None);
    raw->deliverOffThread(std::vector<std::int16_t>(1920, 0));
    CHECK(decodes.load() == 0);
}

// ===========================================================================
// F-1 — the shutdown ordering that AppShell's destructor depends on.
//
// AppShell owns three things: the pipeline, the VoskEngine the pipeline's decoder
// calls into, and the gate. Freeing the engine while the audio thread is inside
// feed() is a use-after-free, and a Phase 3 reviewer reproduced exactly that under
// AddressSanitizer, 7 times out of 7, with both stacks. It cannot happen on the
// development machine — no input device — so it survived to Phase 3 unseen.
//
// The fix is an ORDER: stop the pipeline first, then free what its decoder touches.
// That order is only sound if stop() actually JOINS a callback already in flight,
// which is what these assert.
// ===========================================================================

TEST_CASE("VP/F-1: stop() does not return while the decoder is still running") {
    VoicePipeline p;
    p.setCapture(std::make_unique<ThreadedFakeCapture>());

    std::atomic<bool> inDecode{false};
    std::atomic<int> entries{0};
    p.setDecoder([&](const std::int16_t*, std::size_t) {
        inDecode = true;
        ++entries;
        // Stands in for VoskEngine::feed(), which is not instantaneous — decoding a
        // buffer is the window in which the engine must not be freed.
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        inDecode = false;
        return QString();
    });

    REQUIRE(p.start() == CaptureError::None);
    // Wait until the decoder is genuinely being entered, so stop() is racing
    // something real rather than an idle thread.
    for (int i = 0; i < 200 && entries.load() == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(entries.load() > 0);

    p.stop();

    // THE assertion. If stop() returns while a decode is in flight, everything the
    // decoder captured — in production, the VoskEngine — is being freed underneath
    // a live call.
    CHECK_FALSE(inDecode.load());
    CHECK_FALSE(p.isRunning());

    // And nothing more arrives afterwards.
    const int settled = entries.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    CHECK(entries.load() == settled);
}

TEST_CASE("VP/F-1: destroying the pipeline joins too — the destructor is a stop()") {
    std::atomic<bool> inDecode{false};
    std::atomic<int> entries{0};
    {
        VoicePipeline p;
        p.setCapture(std::make_unique<ThreadedFakeCapture>());
        p.setDecoder([&](const std::int16_t*, std::size_t) {
            inDecode = true;
            ++entries;
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
            inDecode = false;
            return QString();
        });
        REQUIRE(p.start() == CaptureError::None);
        for (int i = 0; i < 200 && entries.load() == 0; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        REQUIRE(entries.load() > 0);
    } // ~VoicePipeline

    // The locals above outlive the pipeline only because this is a test. In AppShell
    // the equivalent captures are the VoskEngine, which is freed immediately after —
    // so this has to hold before the closing brace, not eventually.
    //
    // Honest about its own strength: this one does NOT distinguish the two ways the
    // join can happen. ~VoicePipeline calls stop(), and then destroys the capture,
    // whose own destructor also stops. Deleting the explicit stop() leaves this test
    // green (measured). Its sibling above is the one that pins that; this pins the
    // weaker but still necessary property that DESTRUCTION joins by some route.
    CHECK_FALSE(inDecode.load());
    const int settled = entries.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    CHECK(entries.load() == settled);
}

// ===========================================================================
// BUG-83 — the real-time audio thread had no exception boundary.
//
// BUG-75 put boundaries on the deck-load and render threads and missed this one:
// the thread that runs CONTINUOUSLY during the talk. onSamples executes inside
// miniaudio's C data callback, and every buffer allocates — a vector for the
// downmix, then a std::string, QByteArray, QJsonDocument and QString inside the
// decoder, then a QMetaCallEvent for the queued hand-off. An exception unwinding out
// of a C-ABI frame is std::terminate: the process dies mid-sentence with the deck on
// the projector.
// ===========================================================================

TEST_CASE("VP/BUG-83: a throwing decoder never escapes the audio callback") {
    VoicePipeline p;
    auto cap = std::make_unique<FakeCapture>();
    FakeCapture* raw = cap.get();
    p.setCapture(std::move(cap));

    std::atomic<int> calls{0};
    p.setDecoder([&](const std::int16_t*, std::size_t) -> QString {
        ++calls;
        throw std::bad_alloc(); // what an allocation on a loaded machine looks like
    });

    QSignalSpy heard(&p, &VoicePipeline::phraseHeard);
    REQUIRE(p.start() == CaptureError::None);
    // deliverOffThread joins the delivering thread, so an escape here would take the
    // test process with it — which is precisely the production failure.
    CHECK_NOTHROW(raw->deliverOffThread(std::vector<std::int16_t>(960 * 2, 0)));
    CHECK(calls.load() == 1);
    CHECK(heard.count() == 0);
}

TEST_CASE("VP/BUG-83: a dropped buffer does not stop the pipeline — the next one decodes") {
    // The talk must survive one bad buffer. Stopping on the first exception would
    // turn a transient allocation failure into voice being dead for the rest of the
    // presentation.
    VoicePipeline p;
    auto cap = std::make_unique<FakeCapture>();
    FakeCapture* raw = cap.get();
    p.setCapture(std::move(cap));

    std::atomic<int> calls{0};
    p.setDecoder([&](const std::int16_t*, std::size_t) -> QString {
        if (++calls == 1) {
            throw std::runtime_error("transient");
        }
        return QStringLiteral("next slide");
    });

    QSignalSpy heard(&p, &VoicePipeline::phraseHeard);
    REQUIRE(p.start() == CaptureError::None);
    raw->deliverOffThread(std::vector<std::int16_t>(960 * 2, 0)); // throws, swallowed
    raw->deliverOffThread(std::vector<std::int16_t>(960 * 2, 0)); // must still work
    pump();
    CHECK(calls.load() == 2);
    CHECK(heard.count() == 1);
    CHECK(p.isRunning());
}

TEST_CASE("VP/BUG-83: a non-standard exception type is contained too") {
    VoicePipeline p;
    auto cap = std::make_unique<FakeCapture>();
    FakeCapture* raw = cap.get();
    p.setCapture(std::move(cap));
    p.setDecoder([](const std::int16_t*, std::size_t) -> QString { throw 42; });
    REQUIRE(p.start() == CaptureError::None);
    CHECK_NOTHROW(raw->deliverOffThread(std::vector<std::int16_t>(960 * 2, 0)));
}
