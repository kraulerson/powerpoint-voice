# Audio capture format conversion (F8a)

<!-- Last Updated: 2026-08-05 -->

## Why this module exists

The presentation runs on a **MacBook Pro M3 Max**. All development and agent testing
happen on a **Mac mini with no microphone**. Every assumption about capture hardware
is therefore written where it cannot be tested and executed where it must not fail.

Karl's constraint, verbatim: *"If it relies on talking to specific hardware instead
of a hardware api, it may fail on the macbook pro."*

So the conversion between "whatever the device gives us" and "what the recogniser
needs" is a set of **pure functions** with no hardware dependency, and the
assumptions live in tests rather than in someone's head.

## The contract

| | Value |
|---|---|
| Recogniser input | **16 000 Hz, 1 channel, signed 16-bit** |
| Accepted device rates | 8 000 – 192 000 Hz |
| Accepted device channels | 1 – 64 |

`toRecognizerFormat(interleaved, frameCount, AudioFormat{rate, channels})` returns
16 kHz mono, or an **empty vector** if the format is implausible. It never guesses.

## What the hardware actually does

- A MacBook Pro's built-in microphone is a **three-element array**, which CoreAudio
  typically presents at **48 kHz**.
- AirPods and USB headsets commonly present **44.1 kHz**, sometimes stereo.
- Nothing guarantees 16 kHz mono, and nothing ever will.

Feeding 48 kHz audio to a 16 kHz model **does not raise an error**. It decodes audio
at three times the intended speed into plausible-looking words — the worst possible
failure for a command recogniser, because it is confident and wrong.

## Two decisions worth knowing

**Multi-channel input is averaged, never sampled on channel 0.** The elements of a
microphone array are not equivalent; taking one discards most of the beam-formed
signal. The sum is accumulated in `int32` because two full-scale `int16` channels
sum to 65 534, and the wrap would be audible as a click on exactly the loudest frames.

**Resampling is linear interpolation.** Not the highest quality available, and it does
not need to be: speech recognition at 16 kHz is insensitive to the difference, and a
dependency-free implementation is one less thing to fail on a machine that cannot be
tested beforehand. Output length rounds half-up so a 48 k → 16 k conversion does not
drop the tail sample of every buffer.

## Security note (audit finding F8a-1)

**A capture device reports its own format and nothing verifies it.** The resampler's
output length scales with `outRate / inRate`, so a device claiming 1 Hz turns one
4096-frame callback into 65 million samples — 131 MB — repeatedly, until the machine
dies. A broken or hostile USB audio interface is the attacker.

The rate and channel bounds above are therefore a **resource cap**, in the same family
as the loader's decompression caps (TM-014/017). Formats outside them are rejected
rather than converted. Verified by an ASan+UBSan fuzz pass over 1132 hostile
format combinations including `INT_MAX` rates and channel counts.

## Testing

`tests/test_audio_format.cpp`, GROUP AF — 10 cases. Note that several assert the
**shape** of the signal (zero-crossing count and peak amplitude of a resampled sine),
not merely its length: a length-only assertion passes for silence, and silence is
exactly what a broken resampler produces.
