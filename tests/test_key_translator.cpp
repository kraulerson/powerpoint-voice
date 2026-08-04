#include <doctest/doctest.h>

#include "present/key_translator.hpp"

using namespace pptv;

namespace {
const KeyContext kPresenting{Mode::Presenting, false};
const KeyContext kPresentingPaused{Mode::Presenting, true};
const KeyContext kHolding{Mode::Holding, false};
const KeyContext kConfirm{Mode::ConfirmQuit, false};

void checkCmd(const KeyAction& a, CommandType t, int slide = 0) {
    REQUIRE(a.command.has_value());
    CHECK(a.command->type == t);
    CHECK(a.command->slideNumber == slide);
    CHECK(a.consumed);
}
} // namespace

// ===========================================================================
// GROUP G — key translation. The keyboard is the audited fallback: if this is
// wrong, the presenter has no way to drive the deck when voice fails.
// ===========================================================================

TEST_CASE("G: the usual advance keys all mean next slide") {
    KeyCommandTranslator t;
    for (int k : {Qt::Key_Right, Qt::Key_Space, Qt::Key_Down, Qt::Key_PageDown}) {
        checkCmd(t.onKey(k, Qt::NoModifier, kPresenting, 0), CommandType::NextSlide);
    }
    for (int k : {Qt::Key_Left, Qt::Key_Up, Qt::Key_PageUp, Qt::Key_Backspace}) {
        checkCmd(t.onKey(k, Qt::NoModifier, kPresenting, 0), CommandType::PreviousSlide);
    }
}

TEST_CASE("G: P toggles according to the current pause state") {
    KeyCommandTranslator t;
    checkCmd(t.onKey(Qt::Key_P, Qt::NoModifier, kPresenting, 0), CommandType::PausePresentation);
    checkCmd(t.onKey(Qt::Key_P, Qt::NoModifier, kPresentingPaused, 0),
             CommandType::ContinuePresentation);
}

TEST_CASE("G: typing digits then Enter jumps to that slide") {
    KeyCommandTranslator t;
    CHECK_FALSE(t.onKey(Qt::Key_1, Qt::NoModifier, kPresenting, 0).command.has_value());
    CHECK_FALSE(t.onKey(Qt::Key_2, Qt::NoModifier, kPresenting, 100).command.has_value());
    CHECK(t.pendingDigits() == QStringLiteral("12"));
    checkCmd(t.onKey(Qt::Key_Return, Qt::NoModifier, kPresenting, 200), CommandType::GoToSlide, 12);
    CHECK(t.pendingDigits().isEmpty());
}

TEST_CASE("G: a stale digit is discarded rather than jumping somewhere unexpected") {
    KeyCommandTranslator t;
    t.onKey(Qt::Key_1, Qt::NoModifier, kPresenting, 0);
    t.onKey(Qt::Key_2, Qt::NoModifier, kPresenting, 3001); // stale: buffer restarts
    checkCmd(t.onKey(Qt::Key_Return, Qt::NoModifier, kPresenting, 3100), CommandType::GoToSlide, 2);

    KeyCommandTranslator u; // exactly at the boundary the buffer is still fresh
    u.onKey(Qt::Key_1, Qt::NoModifier, kPresenting, 0);
    u.onKey(Qt::Key_2, Qt::NoModifier, kPresenting, 3000);
    checkCmd(u.onKey(Qt::Key_Return, Qt::NoModifier, kPresenting, 3050), CommandType::GoToSlide,
             12);
}

TEST_CASE("G: Enter with nothing typed does nothing at all") {
    KeyCommandTranslator t;
    const auto a = t.onKey(Qt::Key_Return, Qt::NoModifier, kPresenting, 0);
    CHECK_FALSE(a.command.has_value());
    CHECK_FALSE(a.uiRequest.has_value());
    CHECK_FALSE(a.consumed);
}

TEST_CASE("G: the digit buffer is capped and says so, instead of growing") {
    KeyCommandTranslator t;
    for (int i = 0; i < 12; ++i) {
        const auto a = t.onKey(Qt::Key_1, Qt::NoModifier, kPresenting, i * 10);
        CHECK(a.consumed);
        CHECK(t.pendingDigits().size() <= KeyCommandTranslator::kMaxDigits);
    }
    CHECK(t.pendingDigits().size() == KeyCommandTranslator::kMaxDigits);
}

TEST_CASE("G: Esc means three different things, and never quits directly") {
    KeyCommandTranslator t;
    auto a = t.onKey(Qt::Key_Escape, Qt::NoModifier, kPresenting, 0);
    REQUIRE(a.uiRequest.has_value());
    CHECK(*a.uiRequest == UiRequest::RequestHolding);
    a = t.onKey(Qt::Key_Escape, Qt::NoModifier, kHolding, 0);
    REQUIRE(a.uiRequest.has_value());
    CHECK(*a.uiRequest == UiRequest::RequestQuitConfirm);
    a = t.onKey(Qt::Key_Escape, Qt::NoModifier, kConfirm, 0);
    REQUIRE(a.uiRequest.has_value());
    CHECK(*a.uiRequest == UiRequest::CancelQuit);
}

TEST_CASE("G: while the quit prompt is up, ordinary keys do nothing but are consumed") {
    KeyCommandTranslator t;
    for (int k :
         {Qt::Key_Space, Qt::Key_Right, Qt::Key_Left, Qt::Key_Return, Qt::Key_5, Qt::Key_P}) {
        const auto a = t.onKey(k, Qt::NoModifier, kConfirm, 0);
        CHECK(a.consumed);
        CHECK_FALSE(a.command.has_value());
        CHECK_FALSE(a.uiRequest.has_value());
    }
}

TEST_CASE("G: quitting needs a deliberate chord, never a bare Enter or Space") {
    KeyCommandTranslator t;
    const auto q = t.onKey(Qt::Key_Q, Qt::ControlModifier | Qt::ShiftModifier, kConfirm, 0);
    REQUIRE(q.uiRequest.has_value());
    CHECK(*q.uiRequest == UiRequest::ConfirmQuit);
    for (int k : {Qt::Key_Return, Qt::Key_Space, Qt::Key_Y}) {
        const auto a = t.onKey(k, Qt::NoModifier, kConfirm, 0);
        CHECK_FALSE(a.uiRequest.has_value());
    }
}

TEST_CASE("G: a mode change clears a half-typed slide number") {
    KeyCommandTranslator t;
    t.onKey(Qt::Key_1, Qt::NoModifier, kPresenting, 0);
    t.onKey(Qt::Key_2, Qt::NoModifier, kPresenting, 10);
    REQUIRE(t.pendingDigits() == QStringLiteral("12"));
    t.onModeChanged(Mode::ConfirmQuit);
    CHECK(t.pendingDigits().isEmpty());
}

TEST_CASE("G: operator chords are recognised, and never leak as bare keys") {
    KeyCommandTranslator t;
    const auto mods = Qt::ControlModifier | Qt::ShiftModifier;
    auto a = t.onKey(Qt::Key_D, mods, kPresenting, 0);
    REQUIRE(a.uiRequest.has_value());
    CHECK(*a.uiRequest == UiRequest::MoveSlideWindowToNextScreen);
    a = t.onKey(Qt::Key_F, mods, kPresenting, 0);
    REQUIRE(a.uiRequest.has_value());
    CHECK(*a.uiRequest == UiRequest::ToggleFullScreen);
    a = t.onKey(Qt::Key_R, mods, kPresenting, 0);
    REQUIRE(a.uiRequest.has_value());
    CHECK(*a.uiRequest == UiRequest::ReRenderDeck);

    // the same letters unmodified are NOT commands and are not swallowed
    const auto bare = t.onKey(Qt::Key_D, Qt::NoModifier, kPresenting, 0);
    CHECK_FALSE(bare.consumed);
    CHECK_FALSE(bare.command.has_value());
    CHECK_FALSE(bare.uiRequest.has_value());
}

TEST_CASE("G: a modified arrow key is not a navigation command") {
    KeyCommandTranslator t;
    const auto a = t.onKey(Qt::Key_Right, Qt::ControlModifier, kPresenting, 0);
    CHECK_FALSE(a.command.has_value());
}

TEST_CASE("G: typed slide numbers work from the holding screen too") {
    KeyCommandTranslator t;
    CHECK(t.onKey(Qt::Key_5, Qt::NoModifier, kHolding, 0).consumed);
    CHECK(t.pendingDigits() == QStringLiteral("5"));
    checkCmd(t.onKey(Qt::Key_Return, Qt::NoModifier, kHolding, 100), CommandType::GoToSlide, 5);
}
