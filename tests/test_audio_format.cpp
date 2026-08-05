#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <vector>

#include "audio/audio_format.hpp"

using namespace pptv;

// ===========================================================================
// GROUP AF — capture-format conversion.
//
// These exist because of a deployment fact: the talk runs on a MacBook Pro M3
// Max, and the development machine has no microphone at all. Every assumption
// about what the hardware hands us is therefore untestable where it is written
// and load-bearing where it runs, so the conversion is a pure function and the
// assumptions are pinned here instead.
// ===========================================================================

TEST_CASE("AF: the recogniser's required format is stated, not assumed") {
    CHECK(kRecognizerSampleRate == 16000);
    CHECK(kRecognizerChannels == 1);
    CHECK(AudioFormat{16000, 1}.matchesRecognizer());
    // The two formats a MacBook Pro actually offers must NOT be mistaken for it.
    CHECK_FALSE(AudioFormat{48000, 1}.matchesRecognizer());
    CHECK_FALSE(AudioFormat{48000, 2}.matchesRecognizer());
    CHECK_FALSE(AudioFormat{44100, 2}.matchesRecognizer());
    CHECK_FALSE(AudioFormat{0, 0}.isValid());
}

TEST_CASE("AF: a multi-channel device is MIXED, not sampled on one channel") {
    // A MacBook Pro's built-in microphone is an array. Picking channel 0 would throw
    // away most of the signal, so the channels are averaged.
    const std::int16_t stereo[] = {1000, 2000, -1000, -2000, 0, 0};
    const auto mono = downmixToMono(stereo, 3, 2);
    REQUIRE(mono.size() == 3);
    CHECK(mono[0] == 1500);
    CHECK(mono[1] == -1500);
    CHECK(mono[2] == 0);
}

TEST_CASE("AF: downmix does not overflow on loud input") {
    // Two channels at full scale sum to 65534, which wraps if accumulated in int16 —
    // audible as a loud click on exactly the loudest frames.
    const std::int16_t loud[] = {32767, 32767, -32768, -32768};
    const auto mono = downmixToMono(loud, 2, 2);
    REQUIRE(mono.size() == 2);
    CHECK(mono[0] == 32767);
    CHECK(mono[1] == -32768);
}

TEST_CASE("AF: 48 kHz — a MacBook Pro's native rate — resamples to 16 kHz") {
    std::vector<std::int16_t> in(48000, 0);
    const auto out = resampleMono(in, 48000, 16000);
    CHECK(out.size() == 16000);
}

TEST_CASE("AF: 44.1 kHz — a typical headset — resamples without dropping the tail") {
    std::vector<std::int16_t> in(44100, 0);
    const auto out = resampleMono(in, 44100, 16000);
    CHECK(out.size() == 16000);
}

TEST_CASE("AF: resampling preserves the SHAPE of the signal, not just its length") {
    // A 100 Hz sine at 48 kHz, resampled to 16 kHz, must still be a 100 Hz sine.
    // Length-only assertions would pass for silence, or for garbage.
    std::vector<std::int16_t> in;
    in.reserve(4800);
    for (int i = 0; i < 4800; ++i) {
        in.push_back(static_cast<std::int16_t>(
            10000 * std::sin(2.0 * 3.14159265358979 * 100.0 * i / 48000.0)));
    }
    const auto out = resampleMono(in, 48000, 16000);
    REQUIRE(out.size() == 1600);
    // Count zero crossings: 100 Hz over 0.1 s is 10 cycles => ~20 crossings.
    int crossings = 0;
    for (std::size_t i = 1; i < out.size(); ++i) {
        if ((out[i - 1] < 0) != (out[i] < 0)) {
            ++crossings;
        }
    }
    CHECK(crossings >= 18);
    CHECK(crossings <= 22);
    // And the amplitude survived.
    std::int16_t peak = 0;
    for (std::int16_t v : out) {
        peak = std::max<std::int16_t>(peak, static_cast<std::int16_t>(std::abs(v)));
    }
    CHECK(peak > 9000);
}

TEST_CASE("AF: the full path converts a MacBook Pro's 48 kHz stereo to 16 kHz mono") {
    std::vector<std::int16_t> interleaved(4800 * 2, 0);
    for (int i = 0; i < 4800; ++i) {
        const auto v = static_cast<std::int16_t>(
            8000 * std::sin(2.0 * 3.14159265358979 * 220.0 * i / 48000.0));
        interleaved[static_cast<std::size_t>(i) * 2] = v;
        interleaved[static_cast<std::size_t>(i) * 2 + 1] = v;
    }
    const auto out =
        toRecognizerFormat(interleaved.data(), interleaved.size(), 4800, AudioFormat{48000, 2});
    CHECK(out.size() == 1600);
    std::int16_t peak = 0;
    for (std::int16_t v : out) {
        peak = std::max<std::int16_t>(peak, static_cast<std::int16_t>(std::abs(v)));
    }
    CHECK(peak > 7000); // the signal survived both the downmix and the resample
}

TEST_CASE("AF: a device already at 16 kHz mono is passed through untouched") {
    const std::int16_t in[] = {1, -2, 3, -4};
    const auto out = toRecognizerFormat(in, 4, 4, AudioFormat{16000, 1});
    REQUIRE(out.size() == 4);
    CHECK(out[0] == 1);
    CHECK(out[3] == -4);
}

TEST_CASE("AF: malformed input yields nothing rather than a guess") {
    const std::int16_t one[] = {1};
    CHECK(toRecognizerFormat(nullptr, 100, 10, AudioFormat{48000, 2}).empty());
    CHECK(toRecognizerFormat(one, 1, 0, AudioFormat{48000, 2}).empty());
    CHECK(toRecognizerFormat(one, 1, 1, AudioFormat{0, 2}).empty());     // no rate reported
    CHECK(toRecognizerFormat(one, 1, 1, AudioFormat{48000, 0}).empty()); // no channels
    CHECK(resampleMono({}, 48000, 16000).empty());
    CHECK(resampleMono({1, 2, 3}, 0, 16000).empty());
    CHECK(downmixToMono(nullptr, 4, 2).empty());
}

// Audit finding F8a-1 — a device reports its own format and nothing verifies it.
// The resampler's output length scales with outRate/inRate, so an absurd reported
// rate is an unbounded allocation: at 1 Hz a single 4096-frame callback becomes
// 65 million samples (131 MB), repeatedly, until the machine dies. A broken or
// hostile USB audio interface is the attacker; rejecting is correct because no
// real capture device reports these.
TEST_CASE("AF/audit F8a-1: an implausible device-reported format is REJECTED, not amplified") {
    SUBCASE("rates outside 8 kHz - 192 kHz are not valid formats") {
        CHECK_FALSE(AudioFormat{1, 1}.isValid());
        CHECK_FALSE(AudioFormat{7999, 1}.isValid());
        CHECK_FALSE(AudioFormat{192001, 1}.isValid());
        CHECK_FALSE(AudioFormat{2147483647, 1}.isValid());
        CHECK_FALSE(AudioFormat{-48000, 1}.isValid());
        CHECK(AudioFormat{8000, 1}.isValid());
        CHECK(AudioFormat{48000, 2}.isValid());
        CHECK(AudioFormat{192000, 8}.isValid());
    }

    SUBCASE("an absurd channel count is rejected before it is used as a divisor") {
        CHECK_FALSE(AudioFormat{48000, 0}.isValid());
        CHECK_FALSE(AudioFormat{48000, -1}.isValid());
        CHECK_FALSE(AudioFormat{48000, 65}.isValid());
        CHECK_FALSE(AudioFormat{48000, 2147483647}.isValid());
    }

    SUBCASE("the resampler itself refuses, so no caller can amplify through it") {
        const std::vector<std::int16_t> in(4096, 0);
        CHECK(resampleMono(in, 1, kRecognizerSampleRate).empty());
        CHECK(resampleMono(in, 2147483647, kRecognizerSampleRate).empty());
        CHECK(resampleMono(in, kRecognizerSampleRate, 1).empty());
        // and the legitimate case still works, so the cap is not over-broad
        CHECK(resampleMono(in, 48000, kRecognizerSampleRate).size() > 0);
    }

    SUBCASE("a 1 Hz device cannot allocate through the full conversion path") {
        const std::vector<std::int16_t> buf(4096, 0);
        CHECK(toRecognizerFormat(buf.data(), buf.size(), 2048, AudioFormat{1, 2}).empty());
    }
}

// BUG-56 — the API could not express buffer length, so a device that changed shape
// mid-stream produced a heap over-read. Both events are ones this module's own audit
// predicted: a sample-rate change and a channel-count change.
TEST_CASE("AF/BUG-56: a buffer smaller than the format claims is REFUSED, not over-read") {
    std::vector<std::int16_t> buf(1024, 0);

    SUBCASE("the sample count required by a format is computed, not assumed") {
        CHECK(sampleCountFor(512, AudioFormat{48000, 2}) == 1024);
        CHECK(sampleCountFor(512, AudioFormat{48000, 1}) == 512);
        CHECK(sampleCountFor(0, AudioFormat{48000, 2}) == 0);
        CHECK(sampleCountFor(512, AudioFormat{0, 0}) == 0); // invalid format
    }

    SUBCASE("a frame count that would overflow the multiply yields 0, not a wrap") {
        CHECK(sampleCountFor(std::numeric_limits<std::size_t>::max(), AudioFormat{48000, 2}) == 0);
    }

    SUBCASE("the device says stereo but hands over a mono-sized buffer") {
        // 512 frames x 2 channels = 1024 samples needed; only 512 are available.
        CHECK(toRecognizerFormat(buf.data(), 512, 512, AudioFormat{48000, 2}).empty());
    }

    SUBCASE("the device raises its channel count between describing and delivering") {
        // 1024 samples available, but 8 channels x 512 frames needs 4096.
        CHECK(toRecognizerFormat(buf.data(), buf.size(), 512, AudioFormat{48000, 8}).empty());
    }

    SUBCASE("an exactly-sized buffer is accepted — the check is not over-broad") {
        CHECK_FALSE(toRecognizerFormat(buf.data(), buf.size(), 512, AudioFormat{48000, 2}).empty());
        CHECK_FALSE(
            toRecognizerFormat(buf.data(), buf.size(), 1024, AudioFormat{48000, 1}).empty());
    }
}
