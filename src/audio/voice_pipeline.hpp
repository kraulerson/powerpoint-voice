#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include <QObject>
#include <QString>

#include "audio/audio_capture.hpp"
#include "command/vosk_engine.hpp"

// The join: microphone -> format conversion -> decoder -> phrase (Feature F8d).
//
// This is the only place the three voice layers meet, and it exists so the join
// itself is testable: the capture device and the decoder are both injectable, so
// the threading, buffering and shutdown rules can be exercised on a machine with
// neither a microphone nor a model.
//
// THREADING: audio arrives on a real-time thread. Nothing Qt-related happens there
// — samples are converted and decoded on a dedicated worker, and the recognised
// phrase is delivered to the GUI thread by queued signal. `phraseHeard` therefore
// carries text, which is why it MUST NOT be logged: it is everything said in the
// room (Bible §8, TM-012/013).
namespace pptv {

class VoicePipeline : public QObject {
    Q_OBJECT

  public:
    // The decode step, injectable so tests need no model.
    using DecodeFn = std::function<QString(const std::int16_t*, std::size_t)>;

    explicit VoicePipeline(QObject* parent = nullptr);
    ~VoicePipeline() override;

    void setCapture(std::unique_ptr<IAudioCapture> capture);
    void setDecoder(DecodeFn decode);

    // Starts capture and decoding. Any failure leaves voice off and is reported —
    // it never throws and never stops the talk.
    CaptureError start();
    void stop();
    bool isRunning() const;

  signals:
    // A completed utterance, on the GUI thread. NEVER log this.
    void phraseHeard(const QString& phrase);

  private:
    void onSamples(const std::int16_t* samples, std::size_t count);

    std::unique_ptr<IAudioCapture> capture_;
    DecodeFn decode_;
    std::atomic<bool> running_{false};
};

} // namespace pptv
