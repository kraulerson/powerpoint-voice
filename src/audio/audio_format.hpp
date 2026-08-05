#pragma once

#include <cstdint>
#include <vector>

// Turning whatever the machine's microphone gives us into what the recogniser
// needs — as PURE FUNCTIONS, so the conversion is testable without any audio
// hardware at all. That matters here: the development machine has no microphone,
// and the machine that runs the talk is a different one (a MacBook Pro M3 Max).
//
// THE RULE THIS FILE EXISTS TO ENFORCE: never assume the capture format. The
// recogniser wants 16 kHz mono 16-bit. A MacBook Pro's built-in microphone is a
// three-element array that CoreAudio typically presents at 48 kHz, and an attached
// headset or AirPods can be 44.1 kHz, or stereo, or both. Code that assumes it will
// be handed 16 kHz mono does not fail loudly on the wrong machine — it feeds the
// recogniser audio at three times the intended rate, which decodes as confident
// nonsense. So the device reports its format, and we convert.
namespace pptv {

// What the recogniser requires. Vosk models are trained at a fixed rate; feeding a
// different one silently degrades recognition rather than erroring.
inline constexpr int kRecognizerSampleRate = 16000;
inline constexpr int kRecognizerChannels = 1;

// Plausible bounds on what a capture device may claim. These are a RESOURCE CAP,
// not pedantry: the resampler's output length scales with outRate/inRate, so a
// device reporting 1 Hz turns a 4096-frame callback into 65 million samples —
// 131 MB, per callback, until the machine dies. A device reports its own format
// and nothing verifies it, so a broken or hostile USB interface is the attacker
// here. 8 kHz is telephone quality; 192 kHz is the top of professional audio.
inline constexpr int kMinSampleRate = 8000;
inline constexpr int kMaxSampleRate = 192000;
inline constexpr int kMaxChannels = 64;

struct AudioFormat {
    int sampleRate = 0;
    int channels = 0;

    bool isValid() const {
        return sampleRate >= kMinSampleRate && sampleRate <= kMaxSampleRate && channels >= 1 &&
               channels <= kMaxChannels;
    }
    bool matchesRecognizer() const {
        return sampleRate == kRecognizerSampleRate && channels == kRecognizerChannels;
    }
};

// Average the interleaved channels down to one. A microphone ARRAY presented as
// multiple channels must be mixed, not have one channel picked: on a MacBook Pro
// the elements are not equivalent, and taking channel 0 alone throws away most of
// the beam-formed signal.
std::vector<std::int16_t> downmixToMono(const std::int16_t* interleaved, std::size_t frameCount,
                                        int channels);

// Linear resample to `outRate`. Not the highest-quality method available, and it
// does not need to be: speech recognition at 16 kHz is insensitive to the
// difference, and a dependency-free implementation is one less thing to fail on a
// machine we cannot test on beforehand.
std::vector<std::int16_t> resampleMono(const std::vector<std::int16_t>& in, int inRate,
                                       int outRate);

// The whole conversion: whatever the device gave us -> 16 kHz mono. Returns empty
// for an invalid format or empty input rather than guessing.
std::vector<std::int16_t> toRecognizerFormat(const std::int16_t* interleaved,
                                             std::size_t frameCount, const AudioFormat& in);

} // namespace pptv
