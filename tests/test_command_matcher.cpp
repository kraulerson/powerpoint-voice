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
