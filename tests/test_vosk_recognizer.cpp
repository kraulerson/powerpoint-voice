#include <doctest/doctest.h>

#include <QDir>
#include <QTemporaryDir>

#include "command/command_matcher.hpp"
#include "command/vosk_recognizer.hpp"

using namespace pptv;

// ===========================================================================
// GROUP VR — grammar-constrained recognition.
//
// The property under test is not accuracy. It is that the decoder CANNOT produce
// anything except the five commands. An unconstrained decoder is a 200,000-word
// English model listening to a room of executives, and TM-002/TM-019 is exactly
// that the audience moves the presenter's slides.
//
// Vosk fails OPEN in two ways that emit no error, so both are checked before the
// recogniser may run.
// ===========================================================================

namespace {
QTemporaryDir makeModel(bool dynamicGraph, bool staticGraph = false) {
    QTemporaryDir d;
    QDir(d.path()).mkpath(QStringLiteral("graph"));
    auto touch = [&](const QString& rel) {
        QFile f(d.filePath(rel));
        f.open(QIODevice::WriteOnly);
        f.write("x");
    };
    if (dynamicGraph) {
        touch(QStringLiteral("graph/HCLr.fst"));
        touch(QStringLiteral("graph/Gr.fst"));
    }
    if (staticGraph) {
        touch(QStringLiteral("graph/HCLG.fst"));
    }
    return d;
}
} // namespace

TEST_CASE("VR: a STATIC-graph model is refused — it would ignore the grammar silently") {
    // This is the dangerous case. vosk_recognizer_new_grm succeeds against such a
    // model and then decodes the full vocabulary: no error, no warning, and an
    // audience that can drive the deck.
    QTemporaryDir bad = makeModel(/*dynamic=*/false, /*static=*/true);
    CHECK_FALSE(modelIsGrammarCapable(bad.path()));
    CHECK(prepareRecognizer(bad.path()).error == RecognizerInitError::ModelNotGrammarCapable);
}

TEST_CASE("VR: a dynamic-graph model is accepted") {
    QTemporaryDir good = makeModel(/*dynamic=*/true);
    CHECK(modelIsGrammarCapable(good.path()));
    const auto s = prepareRecognizer(good.path());
    CHECK(s.error == RecognizerInitError::None);
    CHECK_FALSE(s.grammar.empty());
}

TEST_CASE("VR: a half-present dynamic graph is refused, not half-trusted") {
    QTemporaryDir d;
    QDir(d.path()).mkpath(QStringLiteral("graph"));
    QFile f(d.filePath(QStringLiteral("graph/HCLr.fst")));
    f.open(QIODevice::WriteOnly);
    f.write("x"); // Gr.fst absent
    CHECK_FALSE(modelIsGrammarCapable(d.path()));
}

TEST_CASE("VR: a missing model is reported, never assumed present") {
    CHECK(prepareRecognizer(QString()).error == RecognizerInitError::ModelMissing);
    CHECK(prepareRecognizer(QStringLiteral("/nonexistent/model")).error ==
          RecognizerInitError::ModelMissing);
}

TEST_CASE("VR: every init failure names the keyboard and leaks nothing") {
    for (auto e : {RecognizerInitError::ModelMissing, RecognizerInitError::ModelNotGrammarCapable,
                   RecognizerInitError::GrammarRejected, RecognizerInitError::EngineUnavailable}) {
        const QString m = QString::fromUtf8(describeRecognizerInitError(e));
        CHECK_FALSE(m.isEmpty());
        CHECK(m.contains(QStringLiteral("keyboard")));
        CHECK_FALSE(m.contains(QLatin1Char('/'))); // no paths
        CHECK(m.size() < 160);
    }
    CHECK(QString::fromUtf8(describeRecognizerInitError(RecognizerInitError::None)).isEmpty());
}

TEST_CASE("VR: the grammar is valid JSON, built rather than interpolated") {
    // Invalid grammar JSON SEGFAULTS vosk rather than returning an error, so the
    // builder must not be able to emit anything malformed.
    const std::string j = grammarJson(grammarPhrases());
    REQUIRE(j.size() > 2);
    CHECK(j.front() == '[');
    CHECK(j.back() == ']');
    // Balanced quotes, and no character that could break the document.
    int quotes = 0;
    for (char c : j) {
        if (c == '"') {
            ++quotes;
        }
        CHECK(c != '\\');
        CHECK(c != '\n');
    }
    CHECK(quotes % 2 == 0);
}

TEST_CASE("VR: hostile phrases cannot corrupt the grammar document") {
    const std::string j =
        grammarJson({QStringLiteral("next\" , \"rm -rf /"), QStringLiteral("a\\b"),
                     QStringLiteral("tab\there"), QStringLiteral("")});
    for (char c : j) {
        CHECK(c != '\\');
        CHECK(c != '\t');
    }
    int quotes = 0;
    for (char c : j) {
        if (c == '"') {
            ++quotes;
        }
    }
    CHECK(quotes % 2 == 0);
}

TEST_CASE("VR: EVERY grammar phrase is one the matcher accepts") {
    // If the decoder can say something the matcher does not understand, that phrase
    // is dead weight in the grammar; if the matcher accepts something the decoder
    // cannot say, that command is unreachable by voice. The two must not drift.
    for (const QString& phrase : grammarPhrases()) {
        if (phrase.startsWith(QStringLiteral("go to slide"))) {
            continue; // a word list, not a literal utterance
        }
        const auto cmd = matchCommand(phrase);
        CHECK_MESSAGE(cmd.has_value(), "grammar phrase not understood: ", phrase.toStdString());
    }
}

TEST_CASE("VR: the grammar contains NO word outside the five commands") {
    // The whole safety property in one assertion: nothing in the grammar permits an
    // utterance unrelated to driving the deck.
    static const std::vector<QString> allowed = {
        QStringLiteral("next"),      QStringLiteral("previous"),     QStringLiteral("slide"),
        QStringLiteral("pause"),     QStringLiteral("continue"),     QStringLiteral("resume"),
        QStringLiteral("the"),       QStringLiteral("presentation"), QStringLiteral("go"),
        QStringLiteral("to"),        QStringLiteral("zero"),         QStringLiteral("one"),
        QStringLiteral("two"),       QStringLiteral("three"),        QStringLiteral("four"),
        QStringLiteral("five"),      QStringLiteral("six"),          QStringLiteral("seven"),
        QStringLiteral("eight"),     QStringLiteral("nine"),         QStringLiteral("ten"),
        QStringLiteral("eleven"),    QStringLiteral("twelve"),       QStringLiteral("thirteen"),
        QStringLiteral("fourteen"),  QStringLiteral("fifteen"),      QStringLiteral("sixteen"),
        QStringLiteral("seventeen"), QStringLiteral("eighteen"),     QStringLiteral("nineteen"),
        QStringLiteral("twenty"),    QStringLiteral("thirty"),       QStringLiteral("forty"),
        QStringLiteral("fifty"),     QStringLiteral("sixty"),        QStringLiteral("seventy"),
        QStringLiteral("eighty"),    QStringLiteral("ninety"),       QStringLiteral("hundred")};
    for (const QString& phrase : grammarPhrases()) {
        for (const QString& word : phrase.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
            bool ok = false;
            for (const QString& a : allowed) {
                if (word == a) {
                    ok = true;
                }
            }
            CHECK_MESSAGE(ok, "unexpected word in grammar: ", word.toStdString());
        }
    }
}

TEST_CASE("VR: the VENDORED model really is grammar-capable") {
    // Verified once by hand during F8b. A control checked once is not a control:
    // if a future model swap loses the dynamic graph, voice must refuse to run.
    const QString dir = QStringLiteral(PPTV_VOSK_MODEL_DIR);
    if (!QFileInfo(dir).isDir()) {
        WARN("vendored model not extracted; run the pptv_vosk_model target");
        return;
    }
    CHECK(modelIsGrammarCapable(dir));
    CHECK(prepareRecognizer(dir).error == RecognizerInitError::None);
}
