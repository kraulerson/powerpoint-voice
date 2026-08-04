#pragma once

#include <QtGlobal>

#include "command/command_matcher.hpp"
#include "present/notice.hpp"

// The presentation session's decision core (Feature F7) — PURE and headless.
//
// This is the ONLY code in the product that computes a slide index. Both input
// paths (voice, keyboard) funnel through dispatch(), so the BUG-16 range check
// exists in exactly one place and cannot be bypassed.
//
// Pause ownership: this controller does NOT store a pause flag. RecognizerController
// already owns pause and gates navigation BEFORE its sink, so a second pause bit
// here could not be kept in sync (that is precisely how BUG-11 was resurrected in
// design review). Instead the pause state is passed IN per dispatch, and the
// keyboard path never routes through the recognizer at all — so keyboard control
// keeps working while paused (an approved data-contract rule) with no second gate.
namespace pptv {

// Which input produced a command. Voice is gated by pause; the keyboard — the
// audited fallback — is not.
enum class CommandSource { Voice, Keyboard };

// What the session is showing. Quit is reachable ONLY through confirmQuit(), never
// from a Command, so a mis-heard or mis-keyed command cannot end a live talk.
enum class Mode { Idle, Presenting, Holding, ConfirmQuit };

enum class Outcome {
    Moved,      // the slide changed
    NoMove,     // a valid command that does not move (already there, end of deck)
    Rejected,   // out of range / no deck — deliberately NOT clamped
    Suppressed, // gated by pause or by the quit-confirm overlay
};

struct DispatchResult {
    // Rejected, not Suppressed: a command matching no case then fails LOUDLY rather
    // than becoming a silent mid-talk no-op (audit LOW-8).
    Outcome outcome = Outcome::Rejected;
    int slide1Based = 0; // the slide to show after this dispatch
    Notice notice;
    Mode mode = Mode::Idle;
};

class PresentationController {
  public:
    PresentationController() = default;

    // Deck lifecycle. slideCount 0 means "no deck"; every command is then Rejected.
    void setDeck(int slideCount);
    int slideCount() const { return slideCount_; }

    // 1-based for humans/commands, 0-based for the raster cache. Both are always
    // in range (or 0 when there is no deck).
    int currentSlide1Based() const { return slideCount_ == 0 ? 0 : current_ + 1; }
    int currentIndex0Based() const { return slideCount_ == 0 ? 0 : current_; }

    Mode mode() const { return mode_; }
    bool quitConfirmed() const { return quitConfirmed_; }

    // The single funnel. `paused` is the recognizer's pause state (the one owner).
    DispatchResult dispatch(const Command& cmd, CommandSource src, bool paused);

    // UI-initiated transitions (never reachable from a Command). nowMs stamps the
    // ConfirmQuit entry so the overlay can auto-dismiss.
    // nowMs must be MONOTONIC (QElapsedTimer/steady_clock), never a wall clock: a
    // wall-clock step would dismiss or pin the quit prompt. No default — every call
    // site must supply it (audit MEDIUM-3).
    void requestHolding(qint64 nowMs); // Presenting -> Holding -> ConfirmQuit
    void cancelQuit();                 // ConfirmQuit -> Holding
    void confirmQuit();                // the ONLY way quitConfirmed() becomes true
    void onTick(qint64 nowMs);         // auto-dismiss the quit overlay on timeout

    // Undo the most recent JUMP (go-to-slide). Stepwise nav is never undone.
    DispatchResult undoJump();

    static constexpr qint64 kConfirmQuitTimeoutMs = 10000;

  private:
    int slideCount_ = 0;
    int current_ = 0; // 0-based
    Mode mode_ = Mode::Idle;
    bool quitConfirmed_ = false;
    qint64 confirmEnteredMs_ = 0;
    bool haveJumpUndo_ = false;
    int jumpUndoTarget_ = 0;
};

} // namespace pptv
