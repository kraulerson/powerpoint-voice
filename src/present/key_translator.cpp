#include "present/key_translator.hpp"

namespace pptv {

namespace {

// A digit key, or -1. Only the main row is accepted; a keypad digit arrives as the
// same Qt::Key_N, so both work.
int digitFor(int key) {
    return (key >= Qt::Key_0 && key <= Qt::Key_9) ? key - Qt::Key_0 : -1;
}

bool isAdvance(int key) {
    return key == Qt::Key_Right || key == Qt::Key_Space || key == Qt::Key_Down ||
           key == Qt::Key_PageDown;
}

bool isRetreat(int key) {
    return key == Qt::Key_Left || key == Qt::Key_Up || key == Qt::Key_PageUp ||
           key == Qt::Key_Backspace;
}

} // namespace

void KeyCommandTranslator::onModeChanged(Mode) {
    // A half-typed slide number must never survive a mode change: the presenter has
    // moved on, and completing it later would jump somewhere they did not ask for.
    digits_.clear();
    lastDigitMs_ = 0;
}

KeyAction KeyCommandTranslator::onKey(int key, Qt::KeyboardModifiers mods, const KeyContext& ctx,
                                      qint64 nowMs) {
    KeyAction a;

    // The deliberate quit chord is checked FIRST so nothing else can shadow it.
    const bool chord = mods.testFlag(Qt::ControlModifier) && mods.testFlag(Qt::ShiftModifier);
    if (chord && key == Qt::Key_Q) {
        if (ctx.mode == Mode::ConfirmQuit) {
            a.uiRequest = UiRequest::ConfirmQuit;
            a.consumed = true;
        }
        return a;
    }

    // While the quit prompt is up, the ONLY keys that mean anything are Esc (cancel)
    // and the chord above. Everything else is swallowed so a stray press cannot act
    // on the deck behind the overlay — and cannot answer the prompt either.
    if (ctx.mode == Mode::ConfirmQuit) {
        if (key == Qt::Key_Escape) {
            a.uiRequest = UiRequest::CancelQuit;
        }
        a.consumed = true;
        return a;
    }

    if (chord) {
        switch (key) {
        case Qt::Key_D:
            a.uiRequest = UiRequest::MoveSlideWindowToNextScreen;
            a.consumed = true;
            return a;
        case Qt::Key_F:
            a.uiRequest = UiRequest::ToggleFullScreen;
            a.consumed = true;
            return a;
        case Qt::Key_R:
            a.uiRequest = UiRequest::ReRenderDeck;
            a.consumed = true;
            return a;
        default:
            return a; // an unknown chord is not ours; do not swallow it
        }
    }

    // Esc: three meanings, and none of them is "quit". Quitting always costs a
    // second Esc plus the deliberate chord, so one stray press cannot end the talk.
    if (key == Qt::Key_Escape && mods == Qt::NoModifier) {
        a.uiRequest =
            ctx.mode == Mode::Holding ? UiRequest::RequestQuitConfirm : UiRequest::RequestHolding;
        a.consumed = true;
        return a;
    }

    // Typed slide numbers. Digits accumulate; Enter commits.
    const int d = digitFor(key);
    if (d >= 0 && mods == Qt::NoModifier) {
        // Restart rather than extend if the previous digit is stale — otherwise a
        // number typed minutes ago silently prefixes this one.
        if (!digits_.isEmpty() && nowMs - lastDigitMs_ > kDigitStaleMs) {
            digits_.clear();
        }
        if (digits_.size() < kMaxDigits) {
            digits_.append(QChar('0' + d));
        } else {
            a.notice = Notice{NoticeId::SlideNumberTooLong, kMaxDigits, 0, NoticeClass::Transient};
        }
        lastDigitMs_ = nowMs;
        a.consumed = true;
        return a;
    }

    if ((key == Qt::Key_Return || key == Qt::Key_Enter) && mods == Qt::NoModifier) {
        if (digits_.isEmpty()) {
            return a; // nothing typed: not ours, do not swallow
        }
        bool ok = false;
        const int n = digits_.toInt(&ok);
        digits_.clear();
        lastDigitMs_ = 0;
        if (ok) {
            // Range-checking is deliberately NOT done here — PresentationController
            // is the single funnel that rejects out-of-range numbers (BUG-16).
            a.command = Command{CommandType::GoToSlide, n};
        }
        a.consumed = true;
        return a;
    }

    if (mods != Qt::NoModifier) {
        return a; // a modified key is never one of the five commands
    }

    if (isAdvance(key)) {
        a.command = Command{CommandType::NextSlide};
        a.consumed = true;
        return a;
    }
    if (isRetreat(key)) {
        a.command = Command{CommandType::PreviousSlide};
        a.consumed = true;
        return a;
    }
    if (key == Qt::Key_P) {
        // One key toggles, so the presenter never has to remember which state
        // they are in — they just press P again.
        a.command = Command{ctx.paused ? CommandType::ContinuePresentation
                                       : CommandType::PausePresentation};
        a.consumed = true;
        return a;
    }
    return a;
}

} // namespace pptv
