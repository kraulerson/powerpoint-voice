#include "present/presentation_controller.hpp"

namespace pptv {

namespace {

bool isNavigation(CommandType t) {
    return t == CommandType::NextSlide || t == CommandType::PreviousSlide ||
           t == CommandType::GoToSlide;
}

} // namespace

void PresentationController::setDeck(int slideCount) {
    slideCount_ = slideCount > 0 ? slideCount : 0;
    current_ = 0;
    haveJumpUndo_ = false;
    jumpUndoTarget_ = 0;
    mode_ = slideCount_ > 0 ? Mode::Presenting : Mode::Idle;
}

DispatchResult PresentationController::dispatch(const Command& cmd, CommandSource src,
                                                bool paused) {
    DispatchResult r;
    r.mode = mode_;
    r.slide1Based = currentSlide1Based();

    // 1. The quit overlay swallows everything. Nothing behind it may move the deck,
    //    and no command may answer it — confirmQuit() is the only way out.
    if (mode_ == Mode::ConfirmQuit) {
        r.outcome = Outcome::Suppressed;
        return r;
    }

    // 2. No deck: reject rather than inventing a slide.
    if (slideCount_ == 0) {
        r.outcome = Outcome::Rejected;
        r.notice = Notice{NoticeId::DeckEmpty, 0, 0, NoticeClass::Sticky};
        return r;
    }

    // 3. The pause gate is SOURCE-AWARE. Voice navigation is suppressed during Q&A
    //    (TM-002/019); the keyboard — the audited fallback — always works, so the
    //    presenter can never be stranded (this is what keeps BUG-11 dead).
    //    "continue presentation" is never gated: it is the escape hatch itself.
    if (paused && src == CommandSource::Voice && isNavigation(cmd.type)) {
        r.outcome = Outcome::Suppressed;
        r.notice = Notice{NoticeId::Paused, 0, 0, NoticeClass::Sticky};
        return r;
    }

    const int prev1 = current_ + 1;

    switch (cmd.type) {
    case CommandType::GoToSlide: {
        // THE BUG-16 RANGE CHECK — the single place a bad slide number is caught.
        // Reject, never clamp: a mis-heard "go to slide 48" on a 47-slide deck must
        // produce no movement, because a WRONG slide on screen is worse than none.
        if (cmd.slideNumber < 1 || cmd.slideNumber > slideCount_) {
            r.outcome = Outcome::Rejected;
            r.notice = Notice{NoticeId::DeckHasSlides, slideCount_, 0, NoticeClass::Transient};
            break;
        }
        if (cmd.slideNumber == prev1) {
            r.outcome = Outcome::NoMove;
            r.notice = Notice{NoticeId::AlreadyOnSlide, cmd.slideNumber, 0, NoticeClass::Transient};
            break;
        }
        jumpUndoTarget_ = current_; // only a jump is undoable
        haveJumpUndo_ = true;
        current_ = cmd.slideNumber - 1;
        r.outcome = Outcome::Moved;
        r.notice = Notice{NoticeId::CommandEcho, cmd.slideNumber, prev1, NoticeClass::Transient};
        break;
    }
    case CommandType::NextSlide:
        haveJumpUndo_ = false; // stepping away discards the jump-undo
        if (current_ + 1 >= slideCount_) {
            r.outcome = Outcome::NoMove;
            r.notice = Notice{NoticeId::EndOfDeck, prev1, slideCount_, NoticeClass::Transient};
            break;
        }
        ++current_;
        r.outcome = Outcome::Moved;
        r.notice = Notice{NoticeId::CommandEcho, current_ + 1, prev1, NoticeClass::Transient};
        break;
    case CommandType::PreviousSlide:
        haveJumpUndo_ = false;
        if (current_ == 0) {
            r.outcome = Outcome::NoMove;
            r.notice = Notice{NoticeId::AtFirstSlide, 1, slideCount_, NoticeClass::Transient};
            break;
        }
        --current_;
        r.outcome = Outcome::Moved;
        r.notice = Notice{NoticeId::CommandEcho, current_ + 1, prev1, NoticeClass::Transient};
        break;
    case CommandType::PausePresentation:
    case CommandType::ContinuePresentation:
        // Pause state lives in RecognizerController (the single owner); here these
        // are simply not slide movements.
        r.outcome = Outcome::NoMove;
        if (cmd.type == CommandType::ContinuePresentation) {
            // Not gated on `paused`: RecognizerController clears its pause state
            // BEFORE calling the sink, so `paused` is already false here and the
            // signal would never fire (audit MEDIUM-5). BUG-11 was about the
            // presenter not knowing whether voice is live — this is that signal.
            r.notice = Notice{NoticeId::Resumed, 0, 0, NoticeClass::Transient};
        }
        break;
    }

    // Leave the privacy blackout ONLY on an accepted navigation (audit HIGH-2).
    // Previously any command un-blanked it — including a REJECTED one, and a voice
    // "continue presentation" while paused, which handed the audience a way to
    // reveal the deck during Q&A (TM-002/012/019).
    if (mode_ == Mode::Holding && isNavigation(cmd.type) &&
        (r.outcome == Outcome::Moved || r.outcome == Outcome::NoMove)) {
        mode_ = Mode::Presenting;
    }

    r.slide1Based = currentSlide1Based();
    r.mode = mode_;
    return r;
}

void PresentationController::requestHolding(qint64 nowMs) {
    switch (mode_) {
    case Mode::Presenting:
        mode_ = Mode::Holding;
        break;
    case Mode::Holding:
        mode_ = Mode::ConfirmQuit;
        confirmEnteredMs_ = nowMs;
        break;
    case Mode::Idle:
    case Mode::ConfirmQuit:
        break; // asking again while confirming must NOT confirm
    }
}

void PresentationController::cancelQuit() {
    if (mode_ == Mode::ConfirmQuit) {
        mode_ = Mode::Holding;
    }
}

void PresentationController::confirmQuit() {
    // The ONLY assignment to quitConfirmed_ in the product. No Command reaches it.
    if (mode_ == Mode::ConfirmQuit) {
        quitConfirmed_ = true;
    }
}

void PresentationController::onTick(qint64 nowMs) {
    // An unanswered quit prompt must not sit on the projector: it times out back to
    // the holding screen rather than waiting for someone to notice it.
    if (mode_ != Mode::ConfirmQuit) {
        return;
    }
    // A backwards clock step must not dismiss the prompt (audit MEDIUM-4).
    if (nowMs < confirmEnteredMs_) {
        return;
    }
    // Subtract as UNSIGNED. A signed subtraction is UB whenever the operands are far
    // apart (INT64_MAX - INT64_MIN overflows) — clamping the direction alone does not
    // fix that, as ASan/UBSan proved. Unsigned wrap is well-defined, and because
    // nowMs >= confirmEnteredMs_ the difference is the true elapsed time.
    const quint64 elapsed = static_cast<quint64>(nowMs) - static_cast<quint64>(confirmEnteredMs_);
    if (elapsed >= static_cast<quint64>(kConfirmQuitTimeoutMs)) {
        mode_ = Mode::Holding;
    }
}

DispatchResult PresentationController::undoJump() {
    DispatchResult r;
    r.mode = mode_;
    r.slide1Based = currentSlide1Based();
    // The quit overlay swallows everything — undo is an index-computing entry
    // point too, and previously skipped every gate, moving the deck behind the
    // overlay and behind the privacy blackout (audit HIGH-1).
    if (mode_ == Mode::ConfirmQuit) {
        r.outcome = Outcome::Suppressed;
        return r;
    }
    if (!haveJumpUndo_ || slideCount_ == 0) {
        r.outcome = Outcome::NoMove;
        return r;
    }
    if (mode_ == Mode::Holding) {
        mode_ = Mode::Presenting; // never leave the blackout hiding a different slide
    }
    const int prev1 = current_ + 1;
    current_ = jumpUndoTarget_;
    haveJumpUndo_ = false;
    r.outcome = Outcome::Moved;
    r.slide1Based = currentSlide1Based();
    r.notice = Notice{NoticeId::CommandEcho, current_ + 1, prev1, NoticeClass::Transient};
    return r;
}

} // namespace pptv
