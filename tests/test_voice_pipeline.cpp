#include <doctest/doctest.h>

#include <atomic>
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
