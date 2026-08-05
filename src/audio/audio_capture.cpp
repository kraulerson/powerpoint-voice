#include "audio/audio_capture.hpp"

namespace pptv {

const char* describeCaptureError(CaptureError e) {
    switch (e) {
    case CaptureError::None:
        return "";
    case CaptureError::PermissionDenied:
        // Actionable and honest. It does NOT say the app is broken, because it is
        // not: every command remains available from the keyboard.
        return "Microphone access is off. Voice commands are unavailable; the "
               "keyboard still controls the deck.";
    case CaptureError::NoDevice:
        return "No microphone was found. The keyboard still controls the deck.";
    case CaptureError::UnsupportedFormat:
        return "The microphone's audio format is not supported. The keyboard still "
               "controls the deck.";
    case CaptureError::DeviceFailed:
        return "The microphone stopped responding. The keyboard still controls the deck.";
    }
    return "";
}

bool captureErrorIsRecoverable(CaptureError e) {
    // EVERY capture error is recoverable, deliberately. Voice is an enhancement over
    // a keyboard-driven presenter that works on its own; nothing about a microphone
    // failing should be able to stop a talk. This function exists to make that a
    // stated property with a test, rather than an assumption spread across callers.
    (void)e;
    return true;
}

} // namespace pptv
