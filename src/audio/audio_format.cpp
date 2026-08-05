#include "audio/audio_format.hpp"

#include <algorithm>
#include <cstdlib>
#include <limits>

namespace pptv {

std::size_t sampleCountFor(std::size_t frameCount, const AudioFormat& fmt) {
    if (!fmt.isValid() || frameCount == 0) {
        return 0;
    }
    const auto ch = static_cast<std::size_t>(fmt.channels);
    // Reject the multiply if it would wrap. frameCount comes from a device callback
    // and channels from the device's own description; neither is ours to trust.
    if (frameCount > std::numeric_limits<std::size_t>::max() / ch) {
        return 0;
    }
    return frameCount * ch;
}

std::vector<std::int16_t> downmixToMono(const std::int16_t* interleaved, std::size_t frameCount,
                                        int channels) {
    if (interleaved == nullptr || frameCount == 0 || channels < 1 || channels > kMaxChannels) {
        return {};
    }
    std::vector<std::int16_t> out;
    out.reserve(frameCount);
    for (std::size_t f = 0; f < frameCount; ++f) {
        // Accumulate in 32-bit: summing even two int16 channels overflows int16,
        // and the wrap would be heard as a loud click on every loud frame.
        std::int32_t sum = 0;
        for (int c = 0; c < channels; ++c) {
            sum +=
                interleaved[f * static_cast<std::size_t>(channels) + static_cast<std::size_t>(c)];
        }
        out.push_back(static_cast<std::int16_t>(sum / channels));
    }
    return out;
}

std::vector<std::int16_t> resampleMono(const std::vector<std::int16_t>& in, int inRate,
                                       int outRate) {
    // Bound BOTH rates, not just the sign. outCount scales with outRate/inRate, so
    // an unbounded ratio is an unbounded allocation driven by a device-reported
    // number (audit F8a-1). Rejecting is correct: there is no such capture device.
    if (in.empty() || inRate < kMinSampleRate || inRate > kMaxSampleRate ||
        outRate < kMinSampleRate || outRate > kMaxSampleRate) {
        return {};
    }
    if (inRate == outRate) {
        return in;
    }
    // Round-half-up on the output length so a rate ratio like 48000->16000 does not
    // silently drop the tail sample of every buffer; over a talk that adds up.
    const std::size_t outCount = static_cast<std::size_t>(
        (static_cast<long long>(in.size()) * outRate + inRate / 2) / inRate);
    if (outCount == 0) {
        return {};
    }
    std::vector<std::int16_t> out;
    out.reserve(outCount);
    const double step = static_cast<double>(inRate) / static_cast<double>(outRate);
    for (std::size_t i = 0; i < outCount; ++i) {
        const double pos = static_cast<double>(i) * step;
        const std::size_t idx = static_cast<std::size_t>(pos);
        if (idx + 1 < in.size()) {
            const double frac = pos - static_cast<double>(idx);
            const double v = in[idx] * (1.0 - frac) + in[idx + 1] * frac;
            out.push_back(static_cast<std::int16_t>(v));
        } else {
            // Past the last full interval: hold the final sample rather than reading
            // one off the end.
            out.push_back(in.back());
        }
    }
    return out;
}

std::vector<std::int16_t> toRecognizerFormat(const std::int16_t* interleaved,
                                             std::size_t sampleCount, std::size_t frameCount,
                                             const AudioFormat& in) {
    if (interleaved == nullptr || frameCount == 0 || !in.isValid()) {
        return {};
    }
    // The buffer must actually hold what the format says it holds. If the device
    // changed shape between describing itself and delivering, this is where it is
    // caught — the alternative is reading past the end of a real audio buffer.
    const std::size_t needed = sampleCountFor(frameCount, in);
    if (needed == 0 || needed > sampleCount) {
        return {};
    }
    std::vector<std::int16_t> mono =
        (in.channels == 1) ? std::vector<std::int16_t>(interleaved, interleaved + frameCount)
                           : downmixToMono(interleaved, frameCount, in.channels);
    if (in.sampleRate == kRecognizerSampleRate) {
        return mono;
    }
    return resampleMono(mono, in.sampleRate, kRecognizerSampleRate);
}

} // namespace pptv
