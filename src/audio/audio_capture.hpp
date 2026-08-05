#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "audio/audio_format.hpp"

// Microphone capture (Feature F8b).
//
// The interface is separated from the miniaudio backend for one reason above all:
// the development machine HAS NO MICROPHONE, and the machine that runs the talk is
// a different one (a MacBook Pro M3 Max). Every decision this layer makes must
// therefore be testable with a fake device, because the real one is unavailable
// where the code is written and unforgiving where it runs.
//
// Karl's constraint, verbatim: "If it relies on talking to specific hardware
// instead of a hardware api, it may fail on the macbook pro." So the backend binds
// to CoreAudio's DEFAULT input device through miniaudio — never a device index, a
// name, or an enumeration order — and never assumes the capture format.
namespace pptv {

// Why capture is not running. A closed vocabulary: this reaches the presenter, and
// a free-text device name or OS error string would be both useless to them and a
// disclosure channel (Bible §8, TM-012/013).
enum class CaptureError {
    None,
    PermissionDenied,  // macOS microphone permission refused — a NORMAL state
    NoDevice,          // no input device at all
    UnsupportedFormat, // the device reports something we will not convert
    DeviceFailed,      // it started and then stopped
};

// Audio arrives on a REAL-TIME thread. The contract for anything called there:
// no allocation, no locks, no Qt objects, no logging. The sink below is invoked on
// that thread and must obey it.
using CaptureSink = std::function<void(const std::int16_t* samples, std::size_t count)>;

class IAudioCapture {
  public:
    virtual ~IAudioCapture() = default;
    // Begins capture from the DEFAULT input device. Returns the error, or None.
    virtual CaptureError start() = 0;
    virtual void stop() = 0;
    virtual bool isRunning() const = 0;
    // The format the device actually chose — never assumed, always asked for.
    virtual AudioFormat deviceFormat() const = 0;
    virtual void setSink(CaptureSink sink) = 0;
};

// A message for the presenter, from the closed vocabulary above. Never a device
// name, never an OS error string.
const char* describeCaptureError(CaptureError e);

// True when the app can carry on usefully despite this error. Permission denied is
// NOT fatal: the keyboard is the guaranteed control path and the talk must proceed.
bool captureErrorIsRecoverable(CaptureError e);

} // namespace pptv
