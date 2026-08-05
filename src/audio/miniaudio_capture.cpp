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
        running_ = false;
        // Stop BEFORE uninit so no callback is in flight when the device dies, and
        // clear the sink after, so a late callback cannot reach a destroyed capture.
        ma_device_stop(&device_);
        ma_device_uninit(&device_);
        initialised_ = false;
        std::lock_guard<std::mutex> lock(sinkMutex_);
        sink_ = nullptr;
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
        self->sink_(samples, count);
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
