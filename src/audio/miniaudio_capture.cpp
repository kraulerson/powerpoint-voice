#include "audio/miniaudio_capture.hpp"

#include <atomic>
#include <mutex>

#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING
#define MA_NO_DECODING
#define MA_NO_GENERATION
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#include "miniaudio.h"

namespace pptv {
namespace {

class MiniaudioCapture final : public IAudioCapture {
  public:
    ~MiniaudioCapture() override { stop(); }

    CaptureError start() override {
        if (running_.load()) {
            return CaptureError::None;
        }
        ma_device_config cfg = ma_device_config_init(ma_device_type_capture);
        // s16 is what the recogniser consumes, so ask for it and let miniaudio do
        // any sample-TYPE conversion. Channels and rate are left at 0 = "whatever
        // the device natively uses": requesting a rate would make CoreAudio resample
        // for us silently, and we would then have no idea what we were really given.
        cfg.capture.format = ma_format_s16;
        cfg.capture.channels = 0;
        cfg.sampleRate = 0;
        cfg.dataCallback = &MiniaudioCapture::onData;
        cfg.pUserData = this;
        // pDeviceID left null = the system DEFAULT input. Never an index or a name:
        // enumeration order differs between machines and changes when a headset is
        // plugged in.
        cfg.capture.pDeviceID = nullptr;

        if (ma_device_init(nullptr, &cfg, &device_) != MA_SUCCESS) {
            // miniaudio cannot distinguish "denied" from "absent" here, and macOS
            // reports a refused microphone as an init failure. Report the one the
            // presenter can act on; both messages say the keyboard still works.
            return CaptureError::PermissionDenied;
        }
        // Ask the device what it actually chose. Never assume.
        const AudioFormat fmt{static_cast<int>(device_.sampleRate),
                              static_cast<int>(device_.capture.channels)};
        if (!fmt.isValid()) {
            ma_device_uninit(&device_);
            return CaptureError::UnsupportedFormat;
        }
        {
            std::lock_guard<std::mutex> lock(fmtMutex_);
            format_ = fmt;
        }
        if (ma_device_start(&device_) != MA_SUCCESS) {
            ma_device_uninit(&device_);
            return CaptureError::DeviceFailed;
        }
        initialised_ = true;
        running_ = true;
        return CaptureError::None;
    }

    void stop() override {
        if (!initialised_) {
            return;
        }
        // ORDER IS THE WHOLE THING HERE (BUG-79). Quiesce and JOIN the callback
        // first; only then touch the device.
        //
        // The previous order stopped and UNINITIALISED the device and took this lock
        // afterwards, on the stated belief that `ma_device_stop()` does not return
        // until the audio callback has finished. The vendored miniaudio 0.11.25 does
        // not implement that on CoreAudio, and the whole chain is in
        // third_party/miniaudio/miniaudio.h:
        //
        //   * ma_device_stop__coreaudio (36443) calls AudioOutputUnitStop and then
        //     waits on coreaudio.stopEvent (36464). Its own comment above the call
        //     says "It's not clear from the documentation whether or not
        //     AudioOutputUnitStop() actually drains the device or not."
        //   * stopEvent is signalled at exactly one place — the `done:` label of
        //     on_start_stop__coreaudio (35367), a listener on
        //     kAudioOutputUnitProperty_IsRunning. That property changes on START as
        //     well as stop, and the start path falls straight through to `done:`.
        //   * ma_event is LATCHING: ma_event_signal__posix sets value=1 and it
        //     persists; ma_event_wait__posix (17768) returns immediately whenever
        //     value != 0, then auto-resets. Nothing consumes the signal left behind
        //     by device start.
        //
        // So the wait consumes a stale signal from start-up and returns having
        // waited for nothing. And what it waits on is a property-change
        // notification, not the data callback: ma_on_input__coreaudio (35171) checks
        // no device state and takes no lock, so miniaudio offers no barrier here at
        // all. ma_device_uninit then frees pAudioBufferList and MA_ZERO_OBJECTs the
        // device (44119-44178) — and deliberately no longer stops it first, that
        // block is #if 0'd — while our callback may still hold a pointer into that
        // buffer and still be inside sink_().
        //
        // sinkMutex_ IS the barrier, because onData holds it across the entire
        // sink_() call. Taking it here, before the device is touched, is what makes
        // "no callback is in flight" true. Clearing the sink under the same lock
        // means any callback that arrives afterwards finds nothing to call.
        running_ = false;
        {
            std::lock_guard<std::mutex> lock(sinkMutex_);
            sink_ = nullptr;
        }
        ma_device_stop(&device_);
        ma_device_uninit(&device_);
        initialised_ = false;
    }

    bool isRunning() const override { return running_.load(); }

    AudioFormat deviceFormat() const override {
        std::lock_guard<std::mutex> lock(fmtMutex_);
        return format_;
    }

    void setSink(CaptureSink sink) override {
        std::lock_guard<std::mutex> lock(sinkMutex_);
        sink_ = std::move(sink);
    }

  private:
    // REAL-TIME THREAD. No allocation, no Qt, no logging, and the lock here is
    // uncontended in steady state because the sink is set once before start().
    static void onData(ma_device* dev, void* /*out*/, const void* in, ma_uint32 frames) {
        auto* self = static_cast<MiniaudioCapture*>(dev->pUserData);
        if (self == nullptr || in == nullptr || frames == 0 || !self->running_.load()) {
            return;
        }
        std::lock_guard<std::mutex> lock(self->sinkMutex_);
        if (!self->sink_) {
            return;
        }
        const auto* samples = static_cast<const std::int16_t*>(in);
        // The sample count is computed from the device's CURRENT channel count and
        // handed over explicitly, so the consumer can refuse a buffer that does not
        // match rather than reading past it (BUG-56).
        const std::size_t count =
            static_cast<std::size_t>(frames) * static_cast<std::size_t>(dev->capture.channels);
        // The C-ABI boundary itself (BUG-83). VoicePipeline already catches, but this
        // is the frame an exception would actually unwind out of, and the sink is an
        // arbitrary std::function set by whoever owns us. Belt and braces, because the
        // cost of being wrong here is the process dying mid-talk.
        try {
            self->sink_(samples, count);
        } catch (...) {
            // A dropped buffer is 20 ms nobody notices. Nothing is logged — what this
            // callback carries is everything said in the room (TM-012/013).
        }
    }

    ma_device device_{};
    bool initialised_ = false;
    std::atomic<bool> running_{false};
    mutable std::mutex fmtMutex_;
    AudioFormat format_{};
    std::mutex sinkMutex_;
    CaptureSink sink_;
};

} // namespace

std::unique_ptr<IAudioCapture> makeMiniaudioCapture() {
    return std::make_unique<MiniaudioCapture>();
}

} // namespace pptv
