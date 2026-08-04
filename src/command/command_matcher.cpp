#include "command/command_matcher.hpp"

#include <QRegularExpression>

#include "command/number_parser.hpp"

namespace pptv {

namespace {
// The exact trigger prefix for a jump. We require this prefix (rather than
// parsing any number we find) so that ordinary speech containing a number — "we
// have about fifteen minutes" — never becomes a slide jump. Only "go to slide N"
// jumps. Kept as a file-scope constant so it is not rebuilt on every call.
const QString kGoToSlidePrefix = QStringLiteral("go to slide ");
} // namespace

std::optional<Command> matchCommand(const QString& phrase) {
    // Normalize: collapse runs of whitespace, trim ends, and lower-case (via
    // locale-INDEPENDENT Unicode folding — deliberately not QLocale::toLower,
    // which would introduce the Turkish dotless-I hazard). Then strip any
    // leading/trailing punctuation and the whitespace it exposes, so a dictation
    // period or capital ("Next slide.") still fires the command instead of
    // silently no-op'ing (audit M-MED-1). Internal punctuation is left intact, so
    // stripping can only turn a miss into a match, never create a false trigger:
    // matching stays phrase-level (the whole normalized string must be a command).
    QString norm = phrase.simplified().toLower();
    static const QRegularExpression kEdgeJunk(QStringLiteral("^[\\s[:punct:]]+|[\\s[:punct:]]+$"));
    norm.remove(kEdgeJunk);
    if (norm.isEmpty()) {
        return std::nullopt;
    }

    if (norm == QStringLiteral("next slide")) {
        return Command{CommandType::NextSlide};
    }
    if (norm == QStringLiteral("previous slide")) {
        return Command{CommandType::PreviousSlide};
    }
    if (norm == QStringLiteral("pause presentation")) {
        return Command{CommandType::PausePresentation};
    }
    if (norm == QStringLiteral("continue presentation")) {
        return Command{CommandType::ContinuePresentation};
    }

    // "go to slide N": require the exact trigger prefix, then parse the remainder
    // as a number via the F4 parser (which fails safe on missing/malformed
    // numbers). "go to slide" with nothing after it, or an unparseable remainder,
    // is no jump.
    if (norm.startsWith(kGoToSlidePrefix)) {
        const QString rest = norm.mid(kGoToSlidePrefix.size()).trimmed();
        if (const std::optional<int> n = parseSlideNumber(rest)) {
            return Command{CommandType::GoToSlide, *n};
        }
    }
    return std::nullopt;
}

} // namespace pptv
