#include "command/recognizer_controller.hpp"

#include <optional>
#include <utility>

namespace pptv {

RecognizerController::RecognizerController(std::function<void(Command)> sink)
    : sink_(std::move(sink)), state_(State::Active) {}

void RecognizerController::onPhrase(const QString& phrase) {
    // Reentrancy backstop (audit S2): if the sink synchronously feeds another
    // phrase back in, drop it — one top-level onPhrase dispatches at most once and
    // can never recurse without bound.
    if (inDispatch_) {
        return;
    }
    inDispatch_ = true;
    // Reset the guard on every exit path, including an exception from the sink.
    // NOTE: this touches inDispatch_ AFTER the sink runs, so the sink must not
    // destroy this controller during dispatch (documented precondition, audit S5).
    struct GuardReset {
        bool& flag;
        ~GuardReset() { flag = false; }
    } guardReset{inDispatch_};

    const std::optional<Command> cmd = matchCommand(phrase);
    if (!cmd) {
        return; // outside the grammar — ignore
    }

    // Decide whether to emit and commit the state transition FIRST, so a failed or
    // reentrant sink always observes a consistent state (audit S1/S5). The switch
    // is intentionally exhaustive with no default: adding a sixth CommandType then
    // triggers a -Wswitch warning rather than silently falling through (audit S9).
    // (Named shouldDispatch, not "emit" — Qt reserves emit as a macro.)
    bool shouldDispatch = false;
    switch (cmd->type) {
    case CommandType::PausePresentation:
        // Enter Paused once. We keep listening while paused so we can still hear
        // "continue presentation" — only nav is gated, not the mic.
        if (state_ == State::Active) {
            state_ = State::Paused;
            shouldDispatch = true;
        }
        break;
    case CommandType::ContinuePresentation:
        if (state_ == State::Paused) {
            state_ = State::Active;
            shouldDispatch = true;
        }
        break;
    case CommandType::NextSlide:
    case CommandType::PreviousSlide:
    case CommandType::GoToSlide:
        // Navigation acts only while Active. While Paused it is dropped — the
        // audience-Q&A false-trigger protection (TM-002/019).
        shouldDispatch = (state_ == State::Active);
        break;
    }

    if (shouldDispatch && sink_) {
        // Backstop the sink so an exception can never cross the recognizer's
        // (audio-thread) call boundary and std::terminate the app mid-talk
        // (audit S3). The sink owns its own error handling; we log nothing here
        // (no heard text — Bible §8).
        try {
            sink_(*cmd);
        } catch (...) {
            // swallowed by design — see the class contract
        }
    }
}

RecognizerController::State RecognizerController::state() const {
    return state_;
}

} // namespace pptv
