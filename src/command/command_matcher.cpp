#include "command/command_matcher.hpp"

#include <QLatin1Char>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

#include "command/number_parser.hpp"

namespace pptv {

namespace {
// The exact trigger prefix for a jump. We require this prefix (rather than
// parsing any number we find) so that ordinary speech containing a number — "we
// have about fifteen minutes" — never becomes a slide jump. Only "go to slide N"
// jumps. Kept as a file-scope constant so it is not rebuilt on every call.
const QString kGoToSlidePrefix = QStringLiteral("go to slide ");

// Small, SAFE sets of discourse / politeness words stripped from the ENDS of an
// utterance before matching, so natural phrasings ("okay next slide", "next slide
// please") still map (UAT-2 BUG-11/12). Only leading/trailing filler is removed,
// never interior words — so an audience sentence keeps its non-filler words and
// still cannot reduce to a command (the phrase-level defense holds). Deliberately
// EXCLUDES collision-prone words like "the"/"to"/"move" so directional sentences
// ("move to the next slide in our roadmap") remain a safe no-command.
const QSet<QString>& leadingFiller() {
    static const QSet<QString> s = {
        QStringLiteral("okay"),     QStringLiteral("ok"),   QStringLiteral("alright"),
        QStringLiteral("alrighty"), QStringLiteral("so"),   QStringLiteral("um"),
        QStringLiteral("uh"),       QStringLiteral("well"), QStringLiteral("right"),
        QStringLiteral("and"),      QStringLiteral("now"),  QStringLiteral("hey"),
        QStringLiteral("please"),   QStringLiteral("lets"), QStringLiteral("let's"),
        QStringLiteral("yeah"),     QStringLiteral("yep")};
    return s;
}
const QSet<QString>& trailingFiller() {
    static const QSet<QString> s = {
        QStringLiteral("please"), QStringLiteral("thanks"),   QStringLiteral("thank"),
        QStringLiteral("now"),    QStringLiteral("everyone"), QStringLiteral("guys"),
        QStringLiteral("folks"),  QStringLiteral("ok"),       QStringLiteral("okay"),
        QStringLiteral("then"),   QStringLiteral("there"),    QStringLiteral("here")};
    return s;
}
} // namespace

std::optional<Command> matchCommand(const QString& phrase) {
    // Normalize: collapse runs of whitespace, trim ends, and lower-case (via
    // locale-INDEPENDENT Unicode folding — deliberately not QLocale::toLower,
    // which would introduce the Turkish dotless-I hazard). Then strip any
    // leading/trailing punctuation and the whitespace it exposes, so a dictation
    // period or capital ("Next slide.") still fires the command instead of
    // silently no-op'ing (audit M-MED-1).
    QString norm = phrase.simplified().toLower();
    static const QRegularExpression kEdgeJunk(QStringLiteral("^[\\s[:punct:]]+|[\\s[:punct:]]+$"));
    norm.remove(kEdgeJunk);
    if (norm.isEmpty()) {
        return std::nullopt;
    }

    // Strip leading/trailing filler so natural phrasings map (UAT-2 BUG-11/12).
    // Interior words are never touched, so matching stays phrase-level: a sentence
    // that merely contains a keyword cannot reduce to a command.
    QStringList words = norm.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    while (!words.isEmpty() && leadingFiller().contains(words.first())) {
        words.removeFirst();
    }
    while (!words.isEmpty() && trailingFiller().contains(words.last())) {
        words.removeLast();
    }
    if (words.isEmpty()) {
        return std::nullopt;
    }
    const QString core = words.join(QLatin1Char(' '));

    if (core == QStringLiteral("next slide")) {
        return Command{CommandType::NextSlide};
    }
    if (core == QStringLiteral("previous slide")) {
        return Command{CommandType::PreviousSlide};
    }
    // Control commands accept their natural wordings, with or without "the"
    // (UAT-2 BUG-11/12) — but ALWAYS require the object "presentation".
    //
    // BUG-17: a BARE "pause"/"continue"/"resume" is deliberately NOT a command.
    // Accepting one handed the audience a single-word un-pause during Q&A — the
    // exact window the Paused state exists to protect (TM-002/019) — because the
    // filler strip reduces ordinary speech like "okay, let's continue" or "and
    // now continue" to that lone word. Requiring the object makes a conversational
    // word harmless. The residual "stuck in Paused" risk this re-opens is covered
    // by keyboard parity (F6), which is why F6 ships before the voice engine.
    if (core == QStringLiteral("pause presentation") ||
        core == QStringLiteral("pause the presentation")) {
        return Command{CommandType::PausePresentation};
    }
    if (core == QStringLiteral("continue presentation") ||
        core == QStringLiteral("resume presentation") ||
        core == QStringLiteral("continue the presentation") ||
        core == QStringLiteral("resume the presentation")) {
        return Command{CommandType::ContinuePresentation};
    }

    // "go to slide N": require the exact trigger prefix, then parse the remainder
    // as a number via the F4 parser (which fails safe on missing/malformed
    // numbers). "go to slide" with nothing after it, or an unparseable remainder,
    // is no jump.
    if (core.startsWith(kGoToSlidePrefix)) {
        const QString rest = core.mid(kGoToSlidePrefix.size()).trimmed();
        if (const std::optional<int> n = parseSlideNumber(rest)) {
            return Command{CommandType::GoToSlide, *n};
        }
    }
    return std::nullopt;
}

} // namespace pptv
