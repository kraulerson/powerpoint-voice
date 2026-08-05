#include <doctest/doctest.h>

#include <atomic>
#include <thread>
#include <vector>

#include <QString>

#include "audio/audio_capture.hpp"

using namespace pptv;

// ===========================================================================
// GROUP AC — microphone capture.
//
// Every test here uses a FAKE device, on purpose. The development machine has no
// microphone and the talk runs on a different machine entirely, so any behaviour
// that only appears with real hardware is behaviour nobody can check before it
// matters. What CAN be pinned here is the policy: what happens when permission is
// refused, when the device vanishes mid-talk, when it reports a format we will not
// convert — and the answer to all of them must be "the talk continues".
// ===========================================================================

namespace {

// A capture device under the test's control. It runs its sink on a REAL thread,
// because the property that matters most is that the sink is safe to call from one.
class FakeCapture : public IAudioCapture {
  public:
    CaptureError startResult = CaptureError::None;
    AudioFormat format{48000, 2};

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
    void setSink(CaptureSink sink) override { sink_ = std::move(sink); }

    // Deliver one buffer the way a device would: from another thread.
    void deliverOffThread(const std::vector<std::int16_t>& samples) {
        std::thread t([this, samples] {
            if (sink_) {
                sink_(samples.data(), samples.size());
            }
        });
        t.join();
    }

  private:
    CaptureSink sink_;
    std::atomic<bool> running_{false};
};

} // namespace

TEST_CASE("AC: a capture failure NEVER stops the talk") {
    // The keyboard is the guaranteed control path. Voice is an enhancement on top of
    // a presenter that already works, so no microphone problem may be fatal.
    for (CaptureError e :
         {CaptureError::None, CaptureError::PermissionDenied, CaptureError::NoDevice,
          CaptureError::UnsupportedFormat, CaptureError::DeviceFailed}) {
        CHECK(captureErrorIsRecoverable(e));
    }
}

TEST_CASE("AC: every failure has an operator message, and it says the keyboard still works") {
    for (CaptureError e : {CaptureError::PermissionDenied, CaptureError::NoDevice,
                           CaptureError::UnsupportedFormat, CaptureError::DeviceFailed}) {
        const QString msg = QString::fromUtf8(describeCaptureError(e));
        CHECK_FALSE(msg.isEmpty());
        // The presenter's next move must be obvious from the message alone.
        CHECK(msg.contains(QStringLiteral("keyboard")));
    }
    CHECK(QString::fromUtf8(describeCaptureError(CaptureError::None)).isEmpty());
}

TEST_CASE("AC: no failure message leaks a device name, a path, or an OS error string") {
    // These reach the presenter and can land on a projector (Bible section 8,
    // TM-012/013). The vocabulary is closed for that reason.
    for (CaptureError e : {CaptureError::PermissionDenied, CaptureError::NoDevice,
                           CaptureError::UnsupportedFormat, CaptureError::DeviceFailed}) {
        const QString msg = QString::fromUtf8(describeCaptureError(e));
        CHECK_FALSE(msg.contains(QLatin1Char('/')));
        CHECK_FALSE(msg.contains(QStringLiteral("errno")));
        CHECK_FALSE(msg.contains(QStringLiteral("0x")));
        CHECK(msg.size() < 160);
    }
}

TEST_CASE("AC: permission denied is a NORMAL state, not a crash and not a stop") {
    FakeCapture cap;
    cap.startResult = CaptureError::PermissionDenied;
    CHECK(cap.start() == CaptureError::PermissionDenied);
    CHECK_FALSE(cap.isRunning());
    CHECK(captureErrorIsRecoverable(CaptureError::PermissionDenied));
}

TEST_CASE("AC: the device's OWN format is used, never an assumed one") {
    FakeCapture cap;
    // A MacBook Pro's built-in microphone: an array, at 48 kHz.
    cap.format = AudioFormat{48000, 2};
    REQUIRE(cap.start() == CaptureError::None);
    const AudioFormat f = cap.deviceFormat();
    CHECK(f.sampleRate == 48000);
    CHECK(f.channels == 2);
    CHECK_FALSE(f.matchesRecognizer()); // so it MUST be converted
}

TEST_CASE("AC: samples delivered on the device thread convert to recogniser format") {
    FakeCapture cap;
    cap.format = AudioFormat{48000, 2};
    std::atomic<int> callbacks{0};
    std::atomic<std::size_t> converted{0};
    std::atomic<bool> onOtherThread{false};
    const std::thread::id testThread = std::this_thread::get_id();

    cap.setSink([&](const std::int16_t* s, std::size_t n) {
        if (std::this_thread::get_id() != testThread) {
            onOtherThread = true;
        }
        const auto out = toRecognizerFormat(s, n, n / 2, cap.deviceFormat());
        converted += out.size();
        ++callbacks;
    });
    REQUIRE(cap.start() == CaptureError::None);

    std::vector<std::int16_t> buf(960 * 2, 0); // 960 stereo frames @48k = 20 ms
    cap.deliverOffThread(buf);

    CHECK(callbacks.load() == 1);
    CHECK(onOtherThread.load());    // it really came from another thread
    CHECK(converted.load() == 320); // 960 frames @48k -> 320 @16k, mono
}

TEST_CASE("AC: a device that lies about its buffer is refused, not read past") {
    FakeCapture cap;
    cap.format = AudioFormat{48000, 8}; // claims 8 channels...
    std::atomic<std::size_t> converted{0};
    cap.setSink([&](const std::int16_t* s, std::size_t n) {
        // ...but the frame count implies far more samples than were delivered.
        converted += toRecognizerFormat(s, n, n, cap.deviceFormat()).size();
    });
    REQUIRE(cap.start() == CaptureError::None);
    std::vector<std::int16_t> buf(256, 0);
    cap.deliverOffThread(buf);
    CHECK(converted.load() == 0);
}

TEST_CASE("AC: stop() is idempotent and safe when never started") {
    FakeCapture cap;
    cap.stop();
    CHECK_FALSE(cap.isRunning());
    REQUIRE(cap.start() == CaptureError::None);
    CHECK(cap.isRunning());
    cap.stop();
    cap.stop();
    CHECK_FALSE(cap.isRunning());
}
