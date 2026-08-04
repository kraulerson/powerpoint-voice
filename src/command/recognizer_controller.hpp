#pragma once

#include <functional>

#include <QString>

#include "command/command_matcher.hpp"

// Voice-command dispatch (Features F2/F3). Bridges the speech engine to the app:
// consumes recognized phrases, applies matchCommand, and dispatches actionable
// commands to a sink — gated by a listening state so audience speech during Q&A
// cannot move the deck.
namespace pptv {

// Abstract source of recognized phrases. The Vosk + miniaudio adapter implements
// this; tests use a fake that pushes phrases directly. Keeping the engine behind
// this interface is what makes the dispatch logic below unit-testable without
// real audio.
//
// Contract for implementers:
//  - Deliver only FINALIZED phrases — one call per recognized utterance, no
//    partial/interim results — so a single spoken command dispatches exactly once
//    (audit S7: forwarding partials would jump the deck multiple slides).
//  - Marshal the phrase onto the thread that owns the RecognizerController before
//    calling its handler (see the threading note on RecognizerController, S6).
class IRecognizer {
  public:
    virtual ~IRecognizer() = default;
    // Register the sink the recognizer calls with each recognized phrase.
    virtual void setPhraseHandler(std::function<void(const QString&)> handler) = 0;
    virtual void start() = 0;
    virtual void stop() = 0;
};

// Consumes recognized phrases and dispatches commands, gated by a listening
// state:
//   Active  — nav commands (next/previous/go-to) are dispatched; "pause
//             presentation" is dispatched AND transitions to Paused.
//   Paused  — nav commands are IGNORED (audience-Q&A false-trigger protection,
//             TM-002/019); only "continue presentation" is dispatched and
//             transitions back to Active.
// Pausing while paused and continuing while active are no-ops (no duplicate
// emission). Phrases outside the grammar are always ignored. At most one Command
// is emitted per onPhrase() call.
//
// Threading & sink contract (from the F2/F3 security audit):
//  - NOT thread-safe. onPhrase() and state() must be called on the same thread.
//    A recognizer running on its own audio thread must marshal phrases onto this
//    thread (e.g. a Qt queued connection) before calling onPhrase (audit S6:
//    state_ is non-atomic; a concurrent read/write would be a data race).
//  - The sink runs SYNCHRONOUSLY inside onPhrase and owns its own error handling.
//    Any exception it throws is caught and swallowed here so it cannot cross the
//    recognizer's (audio-thread) call boundary and std::terminate the app mid-talk
//    (audit S3). The sink MUST NOT destroy this controller during dispatch, and a
//    reentrant onPhrase call from within the sink is dropped as a backstop
//    (audit S2/S5). State is committed BEFORE the sink is called, so a failed or
//    reentrant sink always observes a consistent state.
class RecognizerController {
  public:
    enum class State { Active, Paused };

    explicit RecognizerController(std::function<void(Command)> sink);

    // Feed one recognized phrase; emits at most one Command per the state rules.
    void onPhrase(const QString& phrase);

    State state() const;

  private:
    std::function<void(Command)> sink_;
    State state_;
    bool inDispatch_ = false; // reentrancy backstop (audit S2)
};

} // namespace pptv
