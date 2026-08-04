#include <doctest/doctest.h>

#include <functional>
#include <stdexcept>
#include <vector>

#include <QString>

#include "command/command_matcher.hpp"
#include "command/recognizer_controller.hpp"

using namespace pptv;

namespace {

// A test double for the speech engine: stores the handler the controller
// registers and lets a test "speak" a phrase by invoking it — the same call the
// real Vosk adapter will make. This exercises the IRecognizer boundary, not just
// the controller in isolation.
class FakeRecognizer : public IRecognizer {
  public:
    void setPhraseHandler(std::function<void(const QString&)> handler) override {
        handler_ = std::move(handler);
    }
    void start() override { running_ = true; }
    void stop() override { running_ = false; }

    void speak(const QString& phrase) {
        if (running_ && handler_) {
            handler_(phrase);
        }
    }

  private:
    std::function<void(const QString&)> handler_;
    bool running_ = false;
};

// Wires a FakeRecognizer to a RecognizerController and records every dispatched
// command, so tests can assert exactly what was (and was not) emitted.
struct Harness {
    std::vector<Command> emitted;
    FakeRecognizer recognizer;
    RecognizerController controller;

    Harness() : controller([this](Command c) { emitted.push_back(c); }) {
        recognizer.setPhraseHandler([this](const QString& phrase) { controller.onPhrase(phrase); });
        recognizer.start();
    }
};

} // namespace

// ===========================================================================
// E. Controller dispatch + Active/Paused state machine.
// ===========================================================================

TEST_CASE("active: a nav phrase dispatches exactly one command") {
    Harness h;
    h.recognizer.speak(QStringLiteral("next slide"));
    REQUIRE(h.emitted.size() == 1);
    CHECK(h.emitted[0].type == CommandType::NextSlide);
    CHECK(h.controller.state() == RecognizerController::State::Active);
}

TEST_CASE("active: an unrecognized phrase dispatches nothing") {
    Harness h;
    h.recognizer.speak(QStringLiteral("so let's move to the next section"));
    CHECK(h.emitted.empty());
    CHECK(h.controller.state() == RecognizerController::State::Active);
}

TEST_CASE("active: 'pause presentation' dispatches and enters Paused") {
    Harness h;
    h.recognizer.speak(QStringLiteral("pause presentation"));
    REQUIRE(h.emitted.size() == 1);
    CHECK(h.emitted[0].type == CommandType::PausePresentation);
    CHECK(h.controller.state() == RecognizerController::State::Paused);
}

TEST_CASE("paused: a nav phrase is IGNORED (audience-Q&A protection)") {
    // The core safety property of this feature.
    Harness h;
    h.recognizer.speak(QStringLiteral("pause presentation"));
    h.emitted.clear();
    h.recognizer.speak(QStringLiteral("next slide"));
    h.recognizer.speak(QStringLiteral("go to slide 5"));
    CHECK(h.emitted.empty());
    CHECK(h.controller.state() == RecognizerController::State::Paused);
}

TEST_CASE("paused: 'continue presentation' resumes, then nav works again") {
    Harness h;
    h.recognizer.speak(QStringLiteral("pause presentation"));
    h.emitted.clear();
    h.recognizer.speak(QStringLiteral("continue presentation"));
    REQUIRE(h.emitted.size() == 1);
    CHECK(h.emitted[0].type == CommandType::ContinuePresentation);
    CHECK(h.controller.state() == RecognizerController::State::Active);

    h.emitted.clear();
    h.recognizer.speak(QStringLiteral("next slide"));
    REQUIRE(h.emitted.size() == 1);
    CHECK(h.emitted[0].type == CommandType::NextSlide);
}

TEST_CASE("idempotent: pausing while paused (and continuing while active) are no-ops") {
    Harness h;
    // continue while already active -> no-op
    h.recognizer.speak(QStringLiteral("continue presentation"));
    CHECK(h.emitted.empty());
    CHECK(h.controller.state() == RecognizerController::State::Active);

    // enter paused, then pause again -> no duplicate emission, stays paused
    h.recognizer.speak(QStringLiteral("pause presentation"));
    h.emitted.clear();
    h.recognizer.speak(QStringLiteral("pause presentation"));
    CHECK(h.emitted.empty());
    CHECK(h.controller.state() == RecognizerController::State::Paused);
}

// ===========================================================================
// SECURITY-AUDIT REMEDIATION (Build Loop Step 4) — F2/F3 controller, 2026-08-04.
// ===========================================================================

// S3 — the sink is invoked synchronously inside onPhrase, which the real
// (audio-thread) recognizer calls. An exception must never propagate out of
// onPhrase: crossing that C/audio-thread boundary is UB / std::terminate — a
// crash mid-presentation. State committed before the sink must stay consistent.
TEST_CASE("audit S3: a throwing sink never propagates out of onPhrase") {
    int calls = 0;
    RecognizerController c([&](Command) {
        ++calls;
        throw std::runtime_error("sink boom");
    });
    CHECK_NOTHROW(c.onPhrase(QStringLiteral("next slide")));
    CHECK(calls == 1);
    CHECK(c.state() == RecognizerController::State::Active);

    // Pause commits the state transition before the sink runs; a throwing sink
    // must still leave a consistent, usable controller.
    CHECK_NOTHROW(c.onPhrase(QStringLiteral("pause presentation")));
    CHECK(c.state() == RecognizerController::State::Paused);
}

// S2 — a sink that synchronously re-enters onPhrase must not cause a second
// dispatch (or unbounded recursion). The reentrant delivery is dropped as a
// backstop, preserving "at most one Command per top-level onPhrase".
TEST_CASE("audit S2: a reentrant sink is dropped, dispatching once") {
    int calls = 0;
    bool reentered = false;
    RecognizerController* cp = nullptr;
    RecognizerController c([&](Command) {
        ++calls;
        if (!reentered) { // probe the guard exactly once
            reentered = true;
            cp->onPhrase(QStringLiteral("next slide"));
        }
    });
    cp = &c;
    c.onPhrase(QStringLiteral("next slide"));
    CHECK(calls == 1); // without the guard this would be 2
    CHECK(c.state() == RecognizerController::State::Active);
}

// ===========================================================================
// UAT SESSION 2 REMEDIATION — 2026-08-04. BUG-11: the Paused->Active escape hatch
// must accept natural resume words, so the presenter is never stuck mid-talk.
// ===========================================================================
TEST_CASE("UAT2 BUG-11/17: 'resume presentation' un-pauses; a bare word does not") {
    Harness h;
    h.recognizer.speak(QStringLiteral("pause presentation"));
    REQUIRE(h.controller.state() == RecognizerController::State::Paused);
    h.emitted.clear();

    // The natural wording "resume presentation" must resume — the SEV-1 fix.
    // (BUG-17 narrowed this: a BARE "resume" no longer counts, so an audience
    // member cannot un-pause the deck with one conversational word.)
    h.recognizer.speak(QStringLiteral("resume"));
    CHECK(h.emitted.empty()); // bare word must NOT resume
    CHECK(h.controller.state() == RecognizerController::State::Paused);

    h.recognizer.speak(QStringLiteral("resume presentation"));
    REQUIRE(h.emitted.size() == 1);
    CHECK(h.emitted[0].type == CommandType::ContinuePresentation);
    CHECK(h.controller.state() == RecognizerController::State::Active);

    // ...and navigation works again after resuming.
    h.emitted.clear();
    h.recognizer.speak(QStringLiteral("next slide"));
    REQUIRE(h.emitted.size() == 1);
    CHECK(h.emitted[0].type == CommandType::NextSlide);
}
