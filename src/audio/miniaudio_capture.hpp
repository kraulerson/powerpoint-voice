#pragma once

#include <memory>

#include "audio/audio_capture.hpp"

namespace pptv {

// The real microphone, via miniaudio -> CoreAudio.
//
// Binds to the DEFAULT input device through the API and never to a device index,
// name, or enumeration order: the machine that runs the talk is not the machine
// this was written on, and miniaudio resolves AudioUnit/AudioComponent at runtime.
//
// It asks the device what format it chose rather than requesting one, because a
// MacBook Pro's built-in microphone is a three-element array CoreAudio typically
// presents at 48 kHz — and feeding 48 kHz to a 16 kHz model does not fail, it
// produces confident nonsense.
std::unique_ptr<IAudioCapture> makeMiniaudioCapture();

} // namespace pptv
