#include <doctest/doctest.h>

#include <optional>

#include <QString>

#include "command/command_matcher.hpp"

using namespace pptv;

namespace {
// Assert an expected command (type + slide number). Fails clearly if the phrase
// produced no command at all.
void checkCmd(const std::optional<Command>& c, CommandType type, int slide = 0) {
    REQUIRE(c.has_value());
    CHECK(c->type == type);
    CHECK(c->slideNumber == slide);
}
} // namespace

// ===========================================================================
// A. Closed grammar — the five commands (happy path).
// ===========================================================================
TEST_CASE("matches the five closed-grammar commands") {
    checkCmd(matchCommand(QStringLiteral("next slide")), CommandType::NextSlide);
    checkCmd(matchCommand(QStringLiteral("previous slide")), CommandType::PreviousSlide);
    checkCmd(matchCommand(QStringLiteral("pause presentation")), CommandType::PausePresentation);
    checkCmd(matchCommand(QStringLiteral("continue presentation")),
             CommandType::ContinuePresentation);
    checkCmd(matchCommand(QStringLiteral("go to slide 7")), CommandType::GoToSlide, 7);
    checkCmd(matchCommand(QStringLiteral("go to slide fifteen")), CommandType::GoToSlide, 15);
}

// ===========================================================================
// B. Tolerance — case and whitespace (the recognizer/keyboard vary).
// ===========================================================================
TEST_CASE("is case- and whitespace-tolerant") {
    checkCmd(matchCommand(QStringLiteral("Next Slide")), CommandType::NextSlide);
    checkCmd(matchCommand(QStringLiteral("  previous   slide ")), CommandType::PreviousSlide);
}

// ===========================================================================
// C. Fail-safe / false-trigger defense (TM-002/019). Anything outside the
//    grammar returns nullopt — no accidental navigation.
// ===========================================================================
TEST_CASE("empty and pure-garbage phrases yield no command") {
    CHECK_FALSE(matchCommand(QStringLiteral("")).has_value());
    CHECK_FALSE(matchCommand(QStringLiteral("banana")).has_value());
}

TEST_CASE("an audience sentence containing a keyword does NOT fire (phrase-level)") {
    // The single most important safety property: matching is phrase-level, not
    // substring — a sentence that merely contains "next" must not advance.
    CHECK_FALSE(matchCommand(QStringLiteral("so let's move to the next section")).has_value());
}

TEST_CASE("a partial command (one word of a two-word phrase) yields no command") {
    CHECK_FALSE(matchCommand(QStringLiteral("next")).has_value());
    CHECK_FALSE(matchCommand(QStringLiteral("slide")).has_value());
    CHECK_FALSE(matchCommand(QStringLiteral("presentation")).has_value());
}

TEST_CASE("'go to slide' without a parseable number yields no command (no jump)") {
    CHECK_FALSE(matchCommand(QStringLiteral("go to slide")).has_value());
    CHECK_FALSE(matchCommand(QStringLiteral("go to slide banana")).has_value());
}

// ===========================================================================
// D. GoToSlide number boundary — the matcher parses; range-checking against the
//    deck length is the caller's job (F7), matching the F4 parser contract.
// ===========================================================================
TEST_CASE("'go to slide 0' parses to 0 (caller range-checks), never a crash") {
    checkCmd(matchCommand(QStringLiteral("go to slide 0")), CommandType::GoToSlide, 0);
}

// ===========================================================================
// ORCHESTRATOR ASSERTIONS — approved by Karl Raulerson at the 2026-08-04 F2/F3
// test gate. (Non-GoToSlide commands must always carry slideNumber == 0.)
// ===========================================================================

// Karl #1 — every command that is not a jump reports slideNumber 0, so a stale
// number can never ride along with next/previous/pause/continue.
TEST_CASE("orchestrator: non-jump commands carry slideNumber 0") {
    checkCmd(matchCommand(QStringLiteral("next slide")), CommandType::NextSlide, 0);
    checkCmd(matchCommand(QStringLiteral("pause presentation")), CommandType::PausePresentation, 0);
}

// Karl #2 — "go to slide N" honors spoken number words end-to-end (reuses F4).
TEST_CASE("orchestrator: go-to accepts number words, digit-by-digit, and hundreds") {
    checkCmd(matchCommand(QStringLiteral("go to slide twenty three")), CommandType::GoToSlide, 23);
    checkCmd(matchCommand(QStringLiteral("go to slide one five")), CommandType::GoToSlide, 15);
    checkCmd(matchCommand(QStringLiteral("go to slide one hundred twenty three")),
             CommandType::GoToSlide, 123);
}

// Karl #3 — a malformed number in a go-to rejects the whole command (no wrong
// jump), consistent with the F4 parser's fail-safe behavior.
TEST_CASE("orchestrator: malformed go-to number rejects the command") {
    CHECK_FALSE(matchCommand(QStringLiteral("go to slide fifteen fifteen")).has_value());
    CHECK_FALSE(matchCommand(QStringLiteral("go to slide twenty thirty")).has_value());
}

// ===========================================================================
// SECURITY-AUDIT REMEDIATION (Build Loop Step 4) — F2/F3 matcher, 2026-08-04.
// ===========================================================================

// M-MED-1 — a recognizer/dictation that appends terminal punctuation or
// capitalizes must still fire the command; it previously silently no-op'd
// ("Next slide." -> nullopt), degrading the app's core function on stage. The
// fix strips leading/trailing punctuation without opening any false-trigger.
TEST_CASE("audit M-MED-1: terminal punctuation is tolerated") {
    checkCmd(matchCommand(QStringLiteral("Next slide.")), CommandType::NextSlide);
    checkCmd(matchCommand(QStringLiteral("Previous slide!")), CommandType::PreviousSlide);
    checkCmd(matchCommand(QStringLiteral("Pause presentation.")), CommandType::PausePresentation);
    checkCmd(matchCommand(QStringLiteral("Continue presentation...")),
             CommandType::ContinuePresentation);
    checkCmd(matchCommand(QStringLiteral("Go to slide 5.")), CommandType::GoToSlide, 5);
    checkCmd(matchCommand(QStringLiteral("go to slide twelve.")), CommandType::GoToSlide, 12);
}

// M-MED-1 must NOT weaken the false-trigger defense: stripping edge punctuation
// still leaves an in-sentence keyword as a safe miss, and pure punctuation is no
// command.
TEST_CASE("audit M-MED-1: punctuation tolerance does not leak a false trigger") {
    CHECK_FALSE(matchCommand(QStringLiteral("move to the next slide, please")).has_value());
    CHECK_FALSE(matchCommand(QStringLiteral("!!!")).has_value());
    CHECK_FALSE(matchCommand(QStringLiteral("...")).has_value());
}

// ===========================================================================
// UAT SESSION 2 REMEDIATION — 2026-08-04. BUG-11 (resume synonyms) + BUG-12
// (natural filler/politeness tolerance), Option A (safe subset).
// ===========================================================================

// BUG-11 — the Paused->resume path must accept the natural resume WORDINGS, not
// only the exact "continue presentation" (else the presenter is stuck mid-talk).
// NARROWED by BUG-17: the object ("presentation") is now required — see below.
TEST_CASE("UAT2 BUG-11: two-word resume wordings map to ContinuePresentation") {
    checkCmd(matchCommand(QStringLiteral("resume presentation")),
             CommandType::ContinuePresentation);
    checkCmd(matchCommand(QStringLiteral("continue the presentation")),
             CommandType::ContinuePresentation);
    checkCmd(matchCommand(QStringLiteral("resume the presentation")),
             CommandType::ContinuePresentation);
    checkCmd(matchCommand(QStringLiteral("continue presentation please")),
             CommandType::ContinuePresentation);
    checkCmd(matchCommand(QStringLiteral("okay resume presentation")),
             CommandType::ContinuePresentation);
}

// BUG-12 — common leading/trailing filler and politeness are tolerated on all
// commands (and the "please" asymmetry is gone).
TEST_CASE("UAT2 BUG-12: natural filler/politeness is tolerated") {
    checkCmd(matchCommand(QStringLiteral("okay next slide")), CommandType::NextSlide);
    checkCmd(matchCommand(QStringLiteral("next slide please")), CommandType::NextSlide);
    checkCmd(matchCommand(QStringLiteral("so previous slide")), CommandType::PreviousSlide);
    checkCmd(matchCommand(QStringLiteral("previous slide please")), CommandType::PreviousSlide);
    checkCmd(matchCommand(QStringLiteral("pause the presentation")),
             CommandType::PausePresentation);
    checkCmd(matchCommand(QStringLiteral("pause presentation please")),
             CommandType::PausePresentation);
    checkCmd(matchCommand(QStringLiteral("go to slide five please")), CommandType::GoToSlide, 5);
    checkCmd(matchCommand(QStringLiteral("okay go to slide twelve")), CommandType::GoToSlide, 12);
}

// BUG-12 re-audit — the added filler tolerance must NOT create a false trigger:
// interior words are never stripped, so an audience sentence keeps its non-filler
// words and still cannot reduce to a command. Directional aliases (BUG-13) stay
// deferred (a safe no-command).
TEST_CASE("UAT2 BUG-12: filler tolerance does not leak a false trigger") {
    CHECK_FALSE(
        matchCommand(QStringLiteral("so let's move to the next slide in our roadmap")).has_value());
    CHECK_FALSE(matchCommand(QStringLiteral("let's pause here for questions")).has_value());
    CHECK_FALSE(matchCommand(QStringLiteral("that's the next slide")).has_value());
    CHECK_FALSE(matchCommand(QStringLiteral("the next slide shows our results")).has_value());
    CHECK_FALSE(
        matchCommand(QStringLiteral("move to the next slide")).has_value()); // BUG-13 deferred
    CHECK_FALSE(matchCommand(QStringLiteral("okay please")).has_value());    // pure filler
}

// ===========================================================================
// BUG-17 (design-review remediation, 2026-08-04) — SINGLE-WORD COMMANDS ARE
// REJECTED. The BUG-11 fix accepted bare "resume"/"continue"/"pause", which
// handed the audience a ONE-WORD un-pause during Q&A — the exact window the
// Paused state exists to protect (TM-002/019). Every command now requires its
// object, so a lone conversational word can never re-arm navigation. The
// residual stuck-in-Paused risk is covered by keyboard parity (F6).
// ===========================================================================
TEST_CASE("BUG-17: bare single-word commands are rejected (Q&A protection)") {
    CHECK_FALSE(matchCommand(QStringLiteral("resume")).has_value());
    CHECK_FALSE(matchCommand(QStringLiteral("continue")).has_value());
    CHECK_FALSE(matchCommand(QStringLiteral("pause")).has_value());
}

// The filler strip must not reduce conversational speech to a lone command word
// and fire it — these are the exact phrasings that used to un-pause the deck.
TEST_CASE("BUG-17: filler cannot reduce speech to a lone command word") {
    CHECK_FALSE(matchCommand(QStringLiteral("okay lets continue")).has_value());
    CHECK_FALSE(matchCommand(QStringLiteral("and now continue")).has_value());
    CHECK_FALSE(matchCommand(QStringLiteral("so continue please")).has_value());
    CHECK_FALSE(matchCommand(QStringLiteral("lets pause")).has_value());
    CHECK_FALSE(matchCommand(QStringLiteral("well resume")).has_value());
}
