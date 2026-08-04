#include <doctest/doctest.h>

#include <QString>

#include "command/number_parser.hpp"

using namespace pptv;

TEST_CASE("parses plain digit strings") {
    CHECK(parseSlideNumber(QStringLiteral("15")) == 15);
    CHECK(parseSlideNumber(QStringLiteral("1")) == 1);
    CHECK(parseSlideNumber(QStringLiteral("150")) == 150);
}

TEST_CASE("parses number words") {
    CHECK(parseSlideNumber(QStringLiteral("fifteen")) == 15);
    CHECK(parseSlideNumber(QStringLiteral("one")) == 1);
    CHECK(parseSlideNumber(QStringLiteral("nine")) == 9);
    CHECK(parseSlideNumber(QStringLiteral("twenty")) == 20);
    CHECK(parseSlideNumber(QStringLiteral("twenty three")) == 23);
    CHECK(parseSlideNumber(QStringLiteral("forty two")) == 42);
}

TEST_CASE("parses hundreds") {
    CHECK(parseSlideNumber(QStringLiteral("one hundred")) == 100);
    CHECK(parseSlideNumber(QStringLiteral("one hundred twenty three")) == 123);
    CHECK(parseSlideNumber(QStringLiteral("two hundred five")) == 205);
}

TEST_CASE("parses digit-by-digit") {
    CHECK(parseSlideNumber(QStringLiteral("one five")) == 15);
    CHECK(parseSlideNumber(QStringLiteral("two zero")) == 20);
    CHECK(parseSlideNumber(QStringLiteral("one zero five")) == 105);
}

TEST_CASE("ignores the go-to-slide filler") {
    CHECK(parseSlideNumber(QStringLiteral("go to slide fifteen")) == 15);
    CHECK(parseSlideNumber(QStringLiteral("go to slide 15")) == 15);
    CHECK(parseSlideNumber(QStringLiteral("slide number twenty one")) == 21);
    CHECK(parseSlideNumber(QStringLiteral("go to slide one five")) == 15);
}

TEST_CASE("is case-insensitive and hyphen-tolerant") {
    CHECK(parseSlideNumber(QStringLiteral("Twenty-Three")) == 23);
    CHECK(parseSlideNumber(QStringLiteral("FIFTEEN")) == 15);
}

TEST_CASE("returns nullopt for no parseable number") {
    CHECK_FALSE(parseSlideNumber(QStringLiteral("")).has_value());
    CHECK_FALSE(parseSlideNumber(QStringLiteral("go to slide")).has_value());
    CHECK_FALSE(parseSlideNumber(QStringLiteral("banana")).has_value());
    CHECK_FALSE(parseSlideNumber(QStringLiteral("next slide")).has_value());
}

// ===========================================================================
// ORCHESTRATOR ASSERTIONS (Build Loop Step 1) — chosen by Karl Raulerson at the
// 2026-08-03 F4 test gate.
// ===========================================================================

// Karl #1 — digit-by-digit resolves by concatenation, not sum.
TEST_CASE("orchestrator: 'one five' is 15, not 6") {
    CHECK(parseSlideNumber(QStringLiteral("one five")) == 15);
    CHECK(parseSlideNumber(QStringLiteral("three seven")) == 37);
}

// Karl #2 — the teen/tens distinction is never confused.
TEST_CASE("orchestrator: fourteen is 14 and forty is 40") {
    CHECK(parseSlideNumber(QStringLiteral("fourteen")) == 14);
    CHECK(parseSlideNumber(QStringLiteral("forty")) == 40);
    CHECK(parseSlideNumber(QStringLiteral("thirteen")) == 13);
    CHECK(parseSlideNumber(QStringLiteral("thirty")) == 30);
}

// Karl #3 — the full spoken command parses regardless of wording.
TEST_CASE("orchestrator: full 'go to slide N' phrasing works") {
    CHECK(parseSlideNumber(QStringLiteral("go to slide fifteen")) == 15);
    CHECK(parseSlideNumber(QStringLiteral("go to slide 15")) == 15);
    CHECK(parseSlideNumber(QStringLiteral("go to slide one five")) == 15);
}

// Karl #4 — anything without a real number yields no jump.
TEST_CASE("orchestrator: garbage or no number returns nothing") {
    CHECK_FALSE(parseSlideNumber(QStringLiteral("next slide")).has_value());
    CHECK_FALSE(parseSlideNumber(QStringLiteral("banana")).has_value());
    CHECK_FALSE(parseSlideNumber(QStringLiteral("")).has_value());
    CHECK_FALSE(parseSlideNumber(QStringLiteral("go to slide")).has_value());
}

// Karl #5 — whitespace, case, and hyphens are tolerated.
TEST_CASE("orchestrator: whitespace/case/hyphen robust") {
    CHECK(parseSlideNumber(QStringLiteral("  Fifteen  ")) == 15);
    CHECK(parseSlideNumber(QStringLiteral("FIFTEEN")) == 15);
    CHECK(parseSlideNumber(QStringLiteral("Twenty-Three")) == 23);
}

// Karl #6 — an out-of-range number still parses (the caller range-checks).
TEST_CASE("orchestrator: out-of-range number still parses, never guesses") {
    CHECK(parseSlideNumber(QStringLiteral("one hundred")) == 100);
    CHECK(parseSlideNumber(QStringLiteral("ninety nine")) == 99);
}

// Karl #7 — "zero" parses to 0 (which the caller rejects), never a crash or 1.
TEST_CASE("orchestrator: 'zero' is 0, sanely") {
    CHECK(parseSlideNumber(QStringLiteral("zero")) == 0);
    CHECK(parseSlideNumber(QStringLiteral("go to slide zero")) == 0);
}

// ===========================================================================
// SECURITY-AUDIT REMEDIATION (Build Loop Step 4) — F4 parser, 2026-08-03.
// ===========================================================================

// F1 — huge/flooded input never overflows to a negative or absurd slide number;
// it rejects (nullopt) so no wrong/crashing jump.
TEST_CASE("audit F1: overflow / token flood rejects, never negative") {
    QString flood = QStringLiteral("nine");
    for (int i = 0; i < 50; ++i) {
        flood += QStringLiteral(" nine");
    }
    CHECK_FALSE(parseSlideNumber(flood).has_value());
    CHECK_FALSE(parseSlideNumber(QStringLiteral("99999999999")).has_value());
    // A big-but-plausible number still parses.
    CHECK(parseSlideNumber(QStringLiteral("300")) == 300);
}

// F3 — a recognizer stutter or malformed sequence rejects instead of jumping to a
// wrong slide.
TEST_CASE("audit F3: malformed sequences reject, never a wrong jump") {
    CHECK_FALSE(parseSlideNumber(QStringLiteral("fifteen fifteen")).has_value());
    CHECK_FALSE(parseSlideNumber(QStringLiteral("twenty thirty")).has_value());
    CHECK_FALSE(parseSlideNumber(QStringLiteral("one ten")).has_value());
    // Well-formed sequences still parse.
    CHECK(parseSlideNumber(QStringLiteral("twenty three")) == 23);
    CHECK(parseSlideNumber(QStringLiteral("one hundred twenty three")) == 123);
}
