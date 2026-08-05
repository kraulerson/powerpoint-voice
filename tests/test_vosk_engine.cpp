#include <doctest/doctest.h>

#include <QFileInfo>
#include <QString>

#include "command/vosk_engine.hpp"

using namespace pptv;

// ===========================================================================
// GROUP VE — the live decoder.
//
// The pure parts (result parsing, vocabulary checking) are tested directly. The
// LIVE parts need the vendored model, which is extracted at build time, so those
// run for real — this is the first test in the project that loads Vosk.
// ===========================================================================

TEST_CASE("VE: only the text field is taken from a Vosk result") {
    CHECK(textFromVoskResult("{\"text\" : \"next slide\"}") == QStringLiteral("next slide"));
    CHECK(textFromVoskResult("{\"text\" : \"\"}").isEmpty());
    // Confidence, alternatives and timings are deliberately ignored.
    CHECK(textFromVoskResult("{\"text\":\"go to slide five\",\"result\":[{\"conf\":0.9}]}") ==
          QStringLiteral("go to slide five"));
}

TEST_CASE("VE: malformed or hostile result documents yield nothing, never a crash") {
    CHECK(textFromVoskResult("").isEmpty());
    CHECK(textFromVoskResult("not json").isEmpty());
    CHECK(textFromVoskResult("[1,2,3]").isEmpty());
    CHECK(textFromVoskResult("{\"text\": 42}").isEmpty());
    CHECK(textFromVoskResult("{\"other\":\"x\"}").isEmpty());
    CHECK(textFromVoskResult(std::string(100000, '{')).isEmpty());
}

TEST_CASE("VE/BUG-65: grammar words the model does not know are REPORTED, not dropped") {
    // Vosk drops unknown grammar tokens silently, which quietly widens what can be
    // recognised. Everything the grammar names must be in the model's vocabulary.
    const std::vector<QString> phrases = {QStringLiteral("next slide"),
                                          QStringLiteral("frobnicate the deck")};
    auto knows = [](const QString& w) { return w != QStringLiteral("frobnicate"); };
    const auto unknown = unknownGrammarWords(phrases, knows);
    REQUIRE(unknown.size() == 1);
    CHECK(unknown[0] == QStringLiteral("frobnicate"));

    SUBCASE("a fully-known grammar reports nothing") {
        CHECK(unknownGrammarWords(phrases, [](const QString&) { return true; }).empty());
    }
    SUBCASE("each unknown word is reported once, not once per occurrence") {
        const std::vector<QString> dup = {QStringLiteral("zzz a"), QStringLiteral("zzz b")};
        CHECK(unknownGrammarWords(dup, [](const QString& w) {
                  return w != QStringLiteral("zzz");
              }).size() == 1);
    }
}

TEST_CASE("VE: a bad setup never starts the engine") {
    VoskEngine e;
    RecognizerSetup bad;
    bad.error = RecognizerInitError::ModelNotGrammarCapable;
    CHECK(e.start(bad) == RecognizerInitError::ModelNotGrammarCapable);
    CHECK_FALSE(e.isRunning());
    CHECK(e.feed(nullptr, 0).isEmpty());
}

TEST_CASE("VE: THE REAL ENGINE loads the vendored model and constrains to the grammar") {
    const QString dir = QStringLiteral(PPTV_VOSK_MODEL_DIR);
    if (!QFileInfo(dir).isDir()) {
        WARN("vendored model not extracted; run the pptv_vosk_model target");
        return;
    }
    const RecognizerSetup setup = prepareRecognizer(dir);
    REQUIRE(setup.error == RecognizerInitError::None);

    VoskEngine e;
    const RecognizerInitError err = e.start(setup);
    // If this fails as GrammarRejected, a grammar word is outside the model's
    // vocabulary — exactly the silent-widening BUG-65 exists to prevent.
    REQUIRE(err == RecognizerInitError::None);
    CHECK(e.isRunning());

    SUBCASE("silence produces no command") {
        std::vector<std::int16_t> quiet(16000, 0); // one second
        const QString out = e.feed(quiet.data(), quiet.size());
        CHECK(out.isEmpty());
    }

    SUBCASE("a zero-length feed is safe") {
        std::int16_t s = 0;
        CHECK(e.feed(&s, 0).isEmpty());
        CHECK(e.feed(nullptr, 100).isEmpty());
    }

    SUBCASE("stop() is idempotent and leaves the engine unusable, not crashing") {
        e.stop();
        CHECK_FALSE(e.isRunning());
        e.stop();
        std::vector<std::int16_t> quiet(1600, 0);
        CHECK(e.feed(quiet.data(), quiet.size()).isEmpty());
    }
}
