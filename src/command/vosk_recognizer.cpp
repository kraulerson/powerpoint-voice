#include "command/vosk_recognizer.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

#include "command/command_matcher.hpp"

namespace pptv {

const char* describeRecognizerInitError(RecognizerInitError e) {
    switch (e) {
    case RecognizerInitError::None:
        return "";
    case RecognizerInitError::ModelMissing:
        return "The speech model is missing. Voice is off; the keyboard still controls the deck.";
    case RecognizerInitError::ModelNotGrammarCapable:
        // Deliberately explicit that voice is OFF. This model would listen to
        // everything, and running anyway is not an option worth offering.
        return "The speech model cannot be limited to the slide commands. Voice is off "
               "for safety; the keyboard still controls the deck.";
    case RecognizerInitError::GrammarRejected:
        return "The command grammar was rejected. Voice is off; the keyboard still "
               "controls the deck.";
    case RecognizerInitError::EngineUnavailable:
        return "The speech engine is unavailable. Voice is off; the keyboard still "
               "controls the deck.";
    }
    return "";
}

std::vector<QString> grammarPhrases() {
    // Derived from the matcher's own accepted phrasings so the decoder cannot be
    // able to say something the matcher would not understand, or vice versa.
    // "go to slide N" is expressed as the stem plus the number words the parser
    // accepts; Vosk grammars are word lists, not patterns.
    std::vector<QString> out = {
        QStringLiteral("next slide"),
        QStringLiteral("previous slide"),
        QStringLiteral("pause presentation"),
        QStringLiteral("pause the presentation"),
        QStringLiteral("continue presentation"),
        QStringLiteral("continue the presentation"),
        QStringLiteral("resume presentation"),
        QStringLiteral("resume the presentation"),
    };
    // The jump command plus every number word the parser understands, so a spoken
    // slide number is inside the grammar rather than outside it.
    static const char* kNumbers[] = {
        "zero",    "one",       "two",      "three",    "four",   "five",     "six",      "seven",
        "eight",   "nine",      "ten",      "eleven",   "twelve", "thirteen", "fourteen", "fifteen",
        "sixteen", "seventeen", "eighteen", "nineteen", "twenty", "thirty",   "forty",    "fifty",
        "sixty",   "seventy",   "eighty",   "ninety",   "hundred"};
    QString jump = QStringLiteral("go to slide");
    for (const char* n : kNumbers) {
        jump += QLatin1Char(' ') + QString::fromLatin1(n);
    }
    out.push_back(jump);
    // MANDATORY. Without it the decoder has no legal path for out-of-grammar audio
    // and must force-fit every cough and audience question onto the nearest command
    // (BUG-67). docs/design-notes/voice-engine-design.md:46 said so before F8c was
    // written; it was not folded in, and 98 of 102 near-miss phrases fired real
    // commands as a result. It reduces rather than removes the hazard — measured
    // 98 -> 85 — so it is a mitigation, not the property once claimed.
    out.push_back(QStringLiteral("[unk]"));
    return out;
}

std::string grammarJson(const std::vector<QString>& phrases) {
    // BUILT, not interpolated: invalid grammar JSON segfaults vosk rather than
    // returning an error, so nothing here may depend on the input being well formed.
    std::string out = "[";
    bool first = true;
    for (const QString& p : phrases) {
        const QString trimmed = p.trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }
        if (!first) {
            out += ", ";
        }
        first = false;
        out += "\"";
        if (trimmed == QStringLiteral("[unk]")) {
            // The ONE bracketed token that must survive. Vosk needs a literal
            // "[unk]" as an escape hatch for out-of-grammar audio; the a-z filter
            // below would silently reduce it to "unk", which Vosk then drops as
            // out-of-vocabulary — leaving no escape hatch and no error (BUG-67).
            out += "[unk]";
        } else {
            for (const QChar& c : trimmed) {
                // Only lower-case ASCII letters and single spaces survive. Anything
                // else is dropped rather than escaped: the grammar is a closed list
                // we author, so an unexpected character means a mistake, not an
                // input to sanitise.
                const char ch = c.toLatin1();
                if ((ch >= 'a' && ch <= 'z') || ch == ' ') {
                    out += ch;
                } else if (ch >= 'A' && ch <= 'Z') {
                    out += static_cast<char>(ch - 'A' + 'a');
                }
            }
        }
        out += "\"";
    }
    out += "]";
    return out;
}

bool modelIsGrammarCapable(const QString& dir) {
    // A dynamic graph is what makes a grammar bind. With graph/HCLG.fst instead,
    // vosk_recognizer_new_grm succeeds and then decodes the FULL vocabulary — no
    // error, no warning, and an audience that can drive the slides.
    const QDir graph(dir + QStringLiteral("/graph"));
    return QFileInfo::exists(graph.filePath(QStringLiteral("HCLr.fst"))) &&
           QFileInfo::exists(graph.filePath(QStringLiteral("Gr.fst")));
}

QString resolveModelDir() {
    // 1. inside the app bundle: <app>/Contents/Resources/vosk-model
    const QDir exeDir(QCoreApplication::applicationDirPath());
    const QString bundled =
        QDir::cleanPath(exeDir.filePath(QStringLiteral("../Resources/vosk-model")));
    if (QFileInfo(bundled).isDir()) {
        return bundled;
    }
    // 2. beside the executable, for a non-bundle layout
    const QString beside = exeDir.filePath(QStringLiteral("vosk-model"));
    if (QFileInfo(beside).isDir()) {
        return beside;
    }
    // 3. the build tree — development only.
    return QStringLiteral(PPTV_VOSK_MODEL_DIR);
}

RecognizerSetup prepareRecognizer(const QString& modelDir) {
    RecognizerSetup s;
    s.modelDir = modelDir;
    if (modelDir.isEmpty() || !QFileInfo(modelDir).isDir()) {
        s.error = RecognizerInitError::ModelMissing;
        return s;
    }
    if (!modelIsGrammarCapable(modelDir)) {
        s.error = RecognizerInitError::ModelNotGrammarCapable;
        return s;
    }
    const auto phrases = grammarPhrases();
    s.grammar = grammarJson(phrases);
    // A grammar that lost every phrase would constrain nothing.
    if (phrases.empty() || s.grammar.size() < 3) {
        s.error = RecognizerInitError::GrammarRejected;
        return s;
    }
    // Validate the ESCAPE HATCH SURVIVED THE BUILDER, not merely that it was asked
    // for. The naive fix here is booby-trapped: the a-z filter turns "[unk]" into
    // "unk", Vosk drops that as out-of-vocabulary, and BUG-65's guard misses it
    // because that guard inspects the phrases rather than the emitted JSON.
    if (s.grammar.find("\"[unk]\"") == std::string::npos) {
        s.error = RecognizerInitError::GrammarRejected;
        return s;
    }
    return s;
}

} // namespace pptv
