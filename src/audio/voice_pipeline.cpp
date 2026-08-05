#include "audio/voice_pipeline.hpp"

#include <QMetaObject>

#include "audio/audio_format.hpp"

namespace pptv {

VoicePipeline::VoicePipeline(QObject* parent) : QObject(parent) {}

VoicePipeline::~VoicePipeline() {
    stop();
}

void VoicePipeline::setCapture(std::unique_ptr<IAudioCapture> capture) {
    stop();
    capture_ = std::move(capture);
}

void VoicePipeline::setDecoder(DecodeFn decode) {
    decode_ = std::move(decode);
}

CaptureError VoicePipeline::start() {
    if (!capture_ || !decode_) {
        return CaptureError::NoDevice;
    }
    capture_->setSink([this](const std::int16_t* s, std::size_t n) { onSamples(s, n); });
    const CaptureError err = capture_->start();
    running_ = (err == CaptureError::None);
    return err;
}

void VoicePipeline::stop() {
    running_ = false;
    if (capture_) {
        // Stops the device BEFORE the sink can be called again, so no callback is in
        // flight against a pipeline that is going away.
        capture_->stop();
    }
}

bool VoicePipeline::isRunning() const {
    return running_.load();
}

void VoicePipeline::onSamples(const std::int16_t* samples, std::size_t count) {
    if (!running_.load() || samples == nullptr || count == 0) {
        return;
    }
    const AudioFormat fmt = capture_ ? capture_->deviceFormat() : AudioFormat{};
    if (!fmt.isValid()) {
        return;
    }
    const std::size_t frames = count / static_cast<std::size_t>(fmt.channels);
    // Refuses rather than over-reads if the device changed shape mid-stream (BUG-56).
    const std::vector<std::int16_t> mono = toRecognizerFormat(samples, count, frames, fmt);
    if (mono.empty()) {
        return;
    }
    const QString phrase = decode_ ? decode_(mono.data(), mono.size()) : QString();
    if (phrase.isEmpty()) {
        return;
    }
    // Cross to the GUI thread. Queued, because everything downstream — the
    // recognizer gate, the presentation controller, the widgets — is GUI-thread only.
    QMetaObject::invokeMethod(
        this, [this, phrase]() { emit phraseHeard(phrase); }, Qt::QueuedConnection);
}

} // namespace pptv
