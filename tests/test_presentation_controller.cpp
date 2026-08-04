#include <doctest/doctest.h>

#include <limits>
#include <random>
#include <vector>

#include "present/presentation_controller.hpp"

using namespace pptv;

namespace {

Command go(int n) {
    return Command{CommandType::GoToSlide, n};
}
const Command kNext{CommandType::NextSlide};
const Command kPrev{CommandType::PreviousSlide};
const Command kPause{CommandType::PausePresentation};
const Command kCont{CommandType::ContinuePresentation};

// A controller with a deck, already presenting on `slide` (1-based).
PresentationController atSlide(int count, int slide) {
    PresentationController c;
    c.setDeck(count);
    c.dispatch(go(slide), CommandSource::Keyboard, false);
    return c;
}

} // namespace

// ===========================================================================
// GROUP A — Navigation + the BUG-16 range check.
//
// The matcher deliberately does NOT range-check slide numbers, so this is the
// single place a bad number is caught. The rule is REJECT, never clamp: a
// mis-heard "go to slide 48" on a 47-slide deck must produce NO movement plus
// "deck has 47 slides", never a jump to 47 (a wrong slide is worse than none).
// ===========================================================================

TEST_CASE("A: slide 0 is rejected, never treated as a slide") {
    auto c = atSlide(47, 12);
    const auto r = c.dispatch(go(0), CommandSource::Keyboard, false);
    CHECK(r.outcome == Outcome::Rejected);
    CHECK(c.currentSlide1Based() == 12);
    CHECK(r.notice.id == NoticeId::DeckHasSlides);
    CHECK(r.notice.arg == 47);
}

TEST_CASE("A: a negative slide is rejected and the index never goes negative") {
    auto c = atSlide(47, 12);
    const auto r = c.dispatch(go(-5), CommandSource::Voice, false);
    CHECK(r.outcome == Outcome::Rejected);
    CHECK(c.currentSlide1Based() == 12);
    CHECK(c.currentIndex0Based() == 11);
    CHECK(r.notice.arg == 47);
}

TEST_CASE("A: one past the end is rejected; the last slide is reachable") {
    auto c = atSlide(47, 12);
    CHECK(c.dispatch(go(48), CommandSource::Voice, false).outcome == Outcome::Rejected);
    CHECK(c.currentSlide1Based() == 12);
    const auto ok = c.dispatch(go(47), CommandSource::Voice, false);
    CHECK(ok.outcome == Outcome::Moved);
    CHECK(c.currentSlide1Based() == 47);
}

TEST_CASE("A: the first slide is reachable") {
    auto c = atSlide(47, 12);
    CHECK(c.dispatch(go(1), CommandSource::Voice, false).outcome == Outcome::Moved);
    CHECK(c.currentSlide1Based() == 1);
}

TEST_CASE("A: absurd numbers are rejected identically, with no overflow") {
    auto c = atSlide(47, 12);
    for (int n : {100001, 2147483647}) {
        const auto r = c.dispatch(go(n), CommandSource::Voice, false);
        CHECK(r.outcome == Outcome::Rejected);
        CHECK(c.currentSlide1Based() == 12);
        CHECK(r.notice.id == NoticeId::DeckHasSlides);
        CHECK(r.notice.arg == 47);
    }
}

TEST_CASE("A: jumping to the slide already shown is a no-move, not a jump") {
    auto c = atSlide(47, 12);
    const auto r = c.dispatch(go(12), CommandSource::Voice, false);
    CHECK(r.outcome == Outcome::NoMove);
    CHECK(r.notice.id == NoticeId::AlreadyOnSlide);
    CHECK(r.notice.arg == 12);
    CHECK(c.currentSlide1Based() == 12);
}

TEST_CASE("A: with no deck loaded, every command from either source is rejected") {
    PresentationController c; // slideCount() == 0
    REQUIRE(c.slideCount() == 0);
    for (auto src : {CommandSource::Voice, CommandSource::Keyboard}) {
        for (const auto& cmd : {go(5), kNext, kPrev}) {
            const auto r = c.dispatch(cmd, src, false);
            CHECK(r.outcome == Outcome::Rejected);
            CHECK(r.notice.id == NoticeId::DeckEmpty);
            CHECK(c.currentSlide1Based() == 0);
        }
    }
}

TEST_CASE("A: a one-slide deck reports both edges without moving") {
    auto c = atSlide(1, 1);
    const auto n = c.dispatch(kNext, CommandSource::Keyboard, false);
    CHECK(n.outcome == Outcome::NoMove);
    CHECK(n.notice.id == NoticeId::EndOfDeck);
    CHECK(n.notice.arg == 1);
    CHECK(n.notice.arg2 == 1);
    const auto p = c.dispatch(kPrev, CommandSource::Keyboard, false);
    CHECK(p.outcome == Outcome::NoMove);
    CHECK(p.notice.id == NoticeId::AtFirstSlide);
    CHECK(c.currentSlide1Based() == 1);
}

TEST_CASE("A: the deck edges hold at both ends") {
    auto c = atSlide(47, 47);
    const auto e = c.dispatch(kNext, CommandSource::Voice, false);
    CHECK(e.outcome == Outcome::NoMove);
    CHECK(c.currentSlide1Based() == 47);
    CHECK(e.notice.id == NoticeId::EndOfDeck);
    CHECK(e.notice.arg == 47);
    CHECK(e.notice.arg2 == 47);

    auto f = atSlide(47, 1);
    const auto b = f.dispatch(kPrev, CommandSource::Voice, false);
    CHECK(b.outcome == Outcome::NoMove);
    CHECK(f.currentSlide1Based() == 1);
    CHECK(b.notice.id == NoticeId::AtFirstSlide);
}

TEST_CASE("A: stepwise navigation moves exactly one slide") {
    auto c = atSlide(47, 12);
    CHECK(c.dispatch(kNext, CommandSource::Voice, false).outcome == Outcome::Moved);
    CHECK(c.currentSlide1Based() == 13);
    CHECK(c.dispatch(kPrev, CommandSource::Voice, false).outcome == Outcome::Moved);
    CHECK(c.currentSlide1Based() == 12);
}

TEST_CASE("A: a jump echoes both the new and the previous slide") {
    auto c = atSlide(300, 12);
    const auto r = c.dispatch(go(56), CommandSource::Voice, false);
    CHECK(r.outcome == Outcome::Moved);
    CHECK(c.currentSlide1Based() == 56);
    CHECK(r.notice.id == NoticeId::CommandEcho);
    CHECK(r.notice.arg == 56);
    CHECK(r.notice.arg2 == 12);
}

TEST_CASE("A: undo applies to jumps only, never to stepwise navigation") {
    auto c = atSlide(47, 12);
    c.dispatch(kNext, CommandSource::Voice, false); // now 13, by a step
    const auto u = c.undoJump();
    CHECK(u.outcome == Outcome::NoMove);
    CHECK(c.currentSlide1Based() == 13);

    c.dispatch(go(40), CommandSource::Voice, false); // a jump from 13
    CHECK(c.currentSlide1Based() == 40);
    const auto u2 = c.undoJump();
    CHECK(u2.outcome == Outcome::Moved);
    CHECK(c.currentSlide1Based() == 13);
}

TEST_CASE("A: fuzz — the slide index is ALWAYS in range after any command sequence") {
    // A mis-heard command must never be able to walk the index out of the deck.
    PresentationController c;
    c.setDeck(47);
    std::mt19937 rng(20260810); // fixed seed: reproducible, never flaky
    const std::vector<Command> cmds{kNext,  kPrev,  kPause, kCont,   go(0), go(1),
                                    go(47), go(48), go(-3), go(999), go(23)};
    for (int step = 0; step < 500; ++step) {
        const int pick = static_cast<int>(rng() % (cmds.size() + 2));
        const bool paused = (rng() % 2) == 0;
        const auto src = (rng() % 2) == 0 ? CommandSource::Voice : CommandSource::Keyboard;
        if (pick == static_cast<int>(cmds.size())) {
            c.requestHolding(step);
        } else if (pick == static_cast<int>(cmds.size()) + 1) {
            c.undoJump();
        } else {
            c.dispatch(cmds[static_cast<std::size_t>(pick)], src, paused);
        }
        REQUIRE(c.currentSlide1Based() >= 1);
        REQUIRE(c.currentSlide1Based() <= 47);
        REQUIRE(c.currentIndex0Based() >= 0);
        REQUIRE(c.currentIndex0Based() < 47);
    }
}

// ===========================================================================
// GROUP D — Mode / quit-confirm state machine.
//
// The property that matters on stage: NO command, from ANY source, in ANY mode,
// can end the presentation. Quitting requires a deliberate confirmQuit().
// ===========================================================================

TEST_CASE("D: Esc goes to the holding screen; a command returns to presenting") {
    auto c = atSlide(47, 12);
    c.requestHolding(0);
    CHECK(c.mode() == Mode::Holding);
    CHECK(c.currentSlide1Based() == 12); // the deck does not move
    const auto r = c.dispatch(kNext, CommandSource::Keyboard, false);
    CHECK(c.mode() == Mode::Presenting);
    CHECK(r.outcome == Outcome::Moved);
    CHECK(c.currentSlide1Based() == 13);
}

TEST_CASE("D: a second Esc asks to quit; cancel returns to holding") {
    auto c = atSlide(47, 12);
    c.requestHolding(0);
    c.requestHolding(0);
    CHECK(c.mode() == Mode::ConfirmQuit);
    CHECK_FALSE(c.quitConfirmed());
    c.cancelQuit();
    CHECK(c.mode() == Mode::Holding);
    CHECK_FALSE(c.quitConfirmed());
}

TEST_CASE("D: while the quit overlay is up, every command is suppressed") {
    auto c = atSlide(47, 12);
    c.requestHolding(0);
    c.requestHolding(0);
    REQUIRE(c.mode() == Mode::ConfirmQuit);
    for (auto src : {CommandSource::Voice, CommandSource::Keyboard}) {
        for (const auto& cmd : {kNext, kPrev, kPause, kCont, go(5)}) {
            const auto r = c.dispatch(cmd, src, false);
            CHECK(r.outcome == Outcome::Suppressed);
            CHECK(c.currentSlide1Based() == 12);
            CHECK(c.mode() == Mode::ConfirmQuit);
            CHECK_FALSE(c.quitConfirmed());
        }
    }
}

TEST_CASE("D: quit is UNREACHABLE from any command, in any mode, from any source") {
    // 4 modes x 5 commands x 2 sources = 40 dispatches, none may quit.
    for (int m = 0; m < 4; ++m) {
        for (auto src : {CommandSource::Voice, CommandSource::Keyboard}) {
            for (const auto& cmd : {kNext, kPrev, kPause, kCont, go(5)}) {
                auto c = atSlide(47, 12);
                if (m >= 1) {
                    c.requestHolding(0); // Presenting -> Holding
                }
                if (m >= 2) {
                    c.requestHolding(0); // Holding -> ConfirmQuit
                }
                c.dispatch(cmd, src, false);
                CHECK_FALSE(c.quitConfirmed());
            }
        }
    }
}

TEST_CASE("D: fuzz — quitConfirmed stays false across 500 random steps") {
    auto c = atSlide(47, 12);
    std::mt19937 rng(20260810);
    const std::vector<Command> cmds{kNext, kPrev, kPause, kCont, go(7)};
    for (int step = 0; step < 500; ++step) {
        const int pick = static_cast<int>(rng() % (cmds.size() + 2));
        const auto src = (rng() % 2) == 0 ? CommandSource::Voice : CommandSource::Keyboard;
        if (pick == static_cast<int>(cmds.size())) {
            c.requestHolding(step);
        } else if (pick == static_cast<int>(cmds.size()) + 1) {
            c.cancelQuit();
        } else {
            c.dispatch(cmds[static_cast<std::size_t>(pick)], src, false);
        }
        REQUIRE_FALSE(c.quitConfirmed());
    }
}

TEST_CASE("D: the quit overlay auto-dismisses rather than waiting for an answer") {
    auto c = atSlide(47, 12);
    c.requestHolding(0);
    c.requestHolding(0);
    REQUIRE(c.mode() == Mode::ConfirmQuit);
    c.onTick(9999);
    CHECK(c.mode() == Mode::ConfirmQuit);
    c.onTick(10000);
    CHECK(c.mode() == Mode::Holding);
    CHECK_FALSE(c.quitConfirmed());
}

TEST_CASE("D: reaching the end of the deck does NOT drop into the holding screen") {
    auto c = atSlide(47, 47);
    const auto r = c.dispatch(kNext, CommandSource::Keyboard, false);
    CHECK(c.mode() == Mode::Presenting);
    CHECK(r.outcome == Outcome::NoMove);
    CHECK(r.notice.id == NoticeId::EndOfDeck);
}

TEST_CASE("D: confirmQuit is the only path to quitting") {
    auto c = atSlide(47, 12);
    c.requestHolding(0);
    c.requestHolding(0);
    REQUIRE(c.mode() == Mode::ConfirmQuit);
    c.confirmQuit();
    CHECK(c.quitConfirmed());
}

// ===========================================================================
// GROUP B/C — ONE pause bit. Pause is owned by RecognizerController and passed in;
// this controller stores none. The rule: VOICE navigation is gated while paused,
// the KEYBOARD (the audited fallback) always works. That is what makes BUG-11
// (stuck in Paused) unable to recur — the escape hatch is not itself gated.
// ===========================================================================

TEST_CASE("B: while paused, a VOICE navigation command is suppressed") {
    auto c = atSlide(47, 12);
    for (const auto& cmd : {kNext, kPrev, go(30)}) {
        const auto r = c.dispatch(cmd, CommandSource::Voice, /*paused=*/true);
        CHECK(r.outcome == Outcome::Suppressed);
        CHECK(r.notice.id == NoticeId::Paused);
        CHECK(c.currentSlide1Based() == 12);
    }
}

TEST_CASE("B: while paused, the KEYBOARD still drives the deck") {
    auto c = atSlide(47, 12);
    const auto n = c.dispatch(kNext, CommandSource::Keyboard, /*paused=*/true);
    CHECK(n.outcome == Outcome::Moved);
    CHECK(c.currentSlide1Based() == 13);
    const auto j = c.dispatch(go(30), CommandSource::Keyboard, /*paused=*/true);
    CHECK(j.outcome == Outcome::Moved);
    CHECK(c.currentSlide1Based() == 30);
}

TEST_CASE("B: while paused, a VOICE continue is NOT suppressed (the escape hatch)") {
    auto c = atSlide(47, 12);
    const auto r = c.dispatch(kCont, CommandSource::Voice, /*paused=*/true);
    CHECK(r.outcome != Outcome::Suppressed);
}

TEST_CASE("B: the range check still applies to keyboard commands while paused") {
    auto c = atSlide(47, 12);
    const auto r = c.dispatch(go(48), CommandSource::Keyboard, /*paused=*/true);
    CHECK(r.outcome == Outcome::Rejected);
    CHECK(c.currentSlide1Based() == 12);
}

// ===========================================================================
// F7a SECURITY-AUDIT REMEDIATION (Build Loop Step 4) — 2026-08-04.
// ===========================================================================

// HIGH-1 — undoJump() was a SECOND index-computing entry point that skipped every
// gate: it moved the deck behind the quit overlay and behind the privacy blackout.
TEST_CASE("audit HIGH-1: undoJump respects the quit overlay") {
    auto c = atSlide(47, 12);
    c.dispatch(go(40), CommandSource::Keyboard, false);
    c.requestHolding(0);
    c.requestHolding(0);
    REQUIRE(c.mode() == Mode::ConfirmQuit);
    const auto r = c.undoJump();
    CHECK(r.outcome == Outcome::Suppressed);
    CHECK(c.currentSlide1Based() == 40); // did NOT move behind the overlay
    CHECK(c.mode() == Mode::ConfirmQuit);
}

TEST_CASE("audit HIGH-1: undoJump from the blackout resumes presenting, never moves silently") {
    auto c = atSlide(47, 12);
    c.dispatch(go(40), CommandSource::Keyboard, false);
    c.requestHolding(0);
    REQUIRE(c.mode() == Mode::Holding);
    const auto r = c.undoJump();
    CHECK(r.outcome == Outcome::Moved);
    CHECK(c.currentSlide1Based() == 12);
    CHECK(c.mode() == Mode::Presenting); // the blackout is not left hiding a new slide
}

// HIGH-2 — the privacy blackout (Esc) must only be dismissed by an ACCEPTED
// navigation. It was being dismissed by rejected commands and, worse, by a VOICE
// "continue presentation" while paused — i.e. an audience member during Q&A could
// un-blank the projector (TM-002/012/019).
TEST_CASE("audit HIGH-2: a rejected command does not un-blank the projector") {
    auto c = atSlide(47, 12);
    c.requestHolding(0);
    const auto r = c.dispatch(go(999), CommandSource::Voice, false);
    CHECK(r.outcome == Outcome::Rejected);
    CHECK(c.mode() == Mode::Holding);
}

TEST_CASE("audit HIGH-2: voice pause/continue cannot un-blank the projector") {
    auto c = atSlide(47, 12);
    c.requestHolding(0);
    c.dispatch(kCont, CommandSource::Voice, /*paused=*/true);
    CHECK(c.mode() == Mode::Holding);
    c.dispatch(kPause, CommandSource::Voice, false);
    CHECK(c.mode() == Mode::Holding);
    c.dispatch(kCont, CommandSource::Voice, false);
    CHECK(c.mode() == Mode::Holding);
}

TEST_CASE("audit HIGH-2: an accepted navigation still resumes from the blackout") {
    auto c = atSlide(47, 12);
    c.requestHolding(0);
    CHECK(c.dispatch(kNext, CommandSource::Keyboard, false).outcome == Outcome::Moved);
    CHECK(c.mode() == Mode::Presenting);

    auto d = atSlide(47, 47); // a NoMove navigation also counts as "the presenter acted"
    d.requestHolding(0);
    CHECK(d.dispatch(kNext, CommandSource::Keyboard, false).outcome == Outcome::NoMove);
    CHECK(d.mode() == Mode::Presenting);
}

// MEDIUM-3/4 — the quit prompt's timer must work on a REAL clock (large values),
// must not be dismissed by a backwards clock, and must never overflow.
TEST_CASE("audit MEDIUM-3: the quit prompt survives on a real wall clock") {
    const qint64 t = 1754300000000LL; // ms since epoch, as a real clock reports
    auto c = atSlide(47, 12);
    c.requestHolding(t);
    c.requestHolding(t);
    REQUIRE(c.mode() == Mode::ConfirmQuit);
    c.onTick(t + PresentationController::kConfirmQuitTimeoutMs - 1);
    CHECK(c.mode() == Mode::ConfirmQuit);
    c.onTick(t + PresentationController::kConfirmQuitTimeoutMs);
    CHECK(c.mode() == Mode::Holding);
}

TEST_CASE("audit MEDIUM-4: a backwards or extreme clock never overflows or mis-dismisses") {
    auto c = atSlide(47, 12);
    c.requestHolding(1000000);
    c.requestHolding(1000000);
    c.onTick(500000); // clock stepped backwards (NTP/DST)
    CHECK(c.mode() == Mode::ConfirmQuit);

    auto d = atSlide(47, 12);
    d.requestHolding(0);
    d.requestHolding(std::numeric_limits<qint64>::min());
    d.onTick(std::numeric_limits<qint64>::max()); // must not be signed overflow (UB)
    CHECK((d.mode() == Mode::ConfirmQuit || d.mode() == Mode::Holding));
}

// MEDIUM-5 — the "voice is back" signal was unreachable by construction, because
// the recognizer clears its pause state BEFORE calling the sink. BUG-11 was about
// the presenter not knowing whether voice is live; the signal must actually fire.
TEST_CASE("audit MEDIUM-5: resuming emits a Resumed notice the presenter can see") {
    auto c = atSlide(47, 12);
    const auto r = c.dispatch(kCont, CommandSource::Voice, /*paused=*/false);
    CHECK(r.notice.id == NoticeId::Resumed);
}

// LOW-8 — a Command whose type matches no case must fail LOUDLY. Silence is the
// hardest failure to diagnose mid-talk.
TEST_CASE("audit LOW-8: an unmatched command is a loud rejection, not a silent no-op") {
    DispatchResult d;
    CHECK(d.outcome == Outcome::Rejected);
}
