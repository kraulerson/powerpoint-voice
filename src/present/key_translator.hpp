#pragma once

#include <optional>

#include <QString>
#include <Qt>

#include "command/command_matcher.hpp"
#include "present/notice.hpp"
#include "present/presentation_controller.hpp"

// Keyboard -> Command translation (Feature F7b; F6 later formalises strict parity).
//
// The keyboard is the AUDITED FALLBACK: when voice fails, mishears, or the room is
// noisy, this is how the presenter drives the deck. It is therefore pure and fully
// unit-tested, and it never routes through the recognizer — so it keeps working
// while voice is paused.
namespace pptv {

// UI actions a key can request. These are NOT Commands: they never move the deck,
// and quitting requires ConfirmQuit, which only the deliberate chord produces.
enum class UiRequest {
    RequestHolding,     // Esc while presenting -> the privacy blackout
    RequestQuitConfirm, // Esc while holding -> the quit prompt
    CancelQuit,         // Esc while the prompt is up
    ConfirmQuit,        // the deliberate chord only
    ToggleFullScreen,
    MoveSlideWindowToNextScreen,
    ReRenderDeck,
};

struct KeyAction {
    std::optional<Command> command;
    std::optional<UiRequest> uiRequest;
    std::optional<Notice> notice;
    bool consumed = false;
};

struct KeyContext {
    Mode mode = Mode::Presenting;
    bool paused = false;
};

// Translates key presses, holding the typed slide-number buffer between calls.
class KeyCommandTranslator {
  public:
    KeyAction onKey(int key, Qt::KeyboardModifiers mods, const KeyContext& ctx, qint64 nowMs);

    // The digit buffer is cleared on every mode change, so a half-typed number can
    // never fire after the presenter has moved on.
    void onModeChanged(Mode newMode);

    QString pendingDigits() const { return digits_; }

    // A slide number typed more than this long ago is stale: the presenter has
    // clearly moved on, and completing it would jump somewhere unexpected.
    static constexpr qint64 kDigitStaleMs = 3000;
    static constexpr int kMaxDigits = 6;

  private:
    QString digits_;
    qint64 lastDigitMs_ = 0;
};

} // namespace pptv
