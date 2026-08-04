#include "command/number_parser.hpp"

#include <QHash>
#include <QRegularExpression>
#include <QStringList>

namespace pptv {
namespace {

// Number-word -> value. Units 0-19, tens, and scale words.
const QHash<QString, int>& wordValues() {
    static const QHash<QString, int> m = {
        {QStringLiteral("zero"), 0},      {QStringLiteral("one"), 1},
        {QStringLiteral("two"), 2},       {QStringLiteral("three"), 3},
        {QStringLiteral("four"), 4},      {QStringLiteral("five"), 5},
        {QStringLiteral("six"), 6},       {QStringLiteral("seven"), 7},
        {QStringLiteral("eight"), 8},     {QStringLiteral("nine"), 9},
        {QStringLiteral("ten"), 10},      {QStringLiteral("eleven"), 11},
        {QStringLiteral("twelve"), 12},   {QStringLiteral("thirteen"), 13},
        {QStringLiteral("fourteen"), 14}, {QStringLiteral("fifteen"), 15},
        {QStringLiteral("sixteen"), 16},  {QStringLiteral("seventeen"), 17},
        {QStringLiteral("eighteen"), 18}, {QStringLiteral("nineteen"), 19},
        {QStringLiteral("twenty"), 20},   {QStringLiteral("thirty"), 30},
        {QStringLiteral("forty"), 40},    {QStringLiteral("fifty"), 50},
        {QStringLiteral("sixty"), 60},    {QStringLiteral("seventy"), 70},
        {QStringLiteral("eighty"), 80},   {QStringLiteral("ninety"), 90},
        {QStringLiteral("hundred"), 100}, {QStringLiteral("thousand"), 1000},
    };
    return m;
}

bool isFiller(const QString& t) {
    static const QStringList f = {
        QStringLiteral("go"),     QStringLiteral("to"),   QStringLiteral("slide"),
        QStringLiteral("number"), QStringLiteral("the"),  QStringLiteral("please"),
        QStringLiteral("hey"),    QStringLiteral("jump"), QStringLiteral("show"),
        QStringLiteral("me"),     QStringLiteral("and"),  QStringLiteral("page")};
    return f.contains(t);
}

// Standard English number evaluation with grammar validation (audit F3): a
// well-formed number within a hundreds/thousands group is a single unit/teen, or
// tens-then-unit. Anything else ("fifteen fifteen", "ten five", "one ten") is a
// recognizer stutter or malformed input and is REJECTED (nullopt) rather than
// summed into a wrong slide. Overflow is bounded (audit F1).
std::optional<int> evalWords(const QList<int>& values) {
    constexpr long long kMax = 100000; // no real deck approaches this
    enum GroupState { Empty, Tens, Done };
    long long result = 0;
    long long current = 0;
    GroupState state = Empty;
    for (int v : values) {
        if (v == 100) {
            current = (current == 0 ? 1 : current) * 100;
            state = Empty;
        } else if (v == 1000) {
            result += (current == 0 ? 1 : current) * 1000;
            current = 0;
            state = Empty;
        } else {
            const bool isTens = (v >= 20 && v % 10 == 0);
            const bool isUnit = (v >= 1 && v <= 9);
            switch (state) {
            case Empty:
                current += v;
                state = isTens ? Tens : Done;
                break;
            case Tens:
                if (!isUnit) {
                    return std::nullopt; // e.g. "twenty thirty"
                }
                current += v;
                state = Done;
                break;
            case Done:
                return std::nullopt; // e.g. "fifteen fifteen", "one ten"
            }
        }
        if (result + current > kMax) {
            return std::nullopt; // overflow / absurd
        }
    }
    return static_cast<int>(result + current);
}

} // namespace

std::optional<int> parseSlideNumber(const QString& spoken) {
    const QStringList raw =
        spoken.toLower().split(QRegularExpression(QStringLiteral("[\\s\\-]+")), Qt::SkipEmptyParts);

    // Keep only number tokens (numerals or number words); drop filler. Any token
    // that is neither filler nor a number aborts the parse (no spurious jump).
    QStringList nums;
    static const QRegularExpression numeral(QStringLiteral("^\\d+$"));
    for (const QString& t : raw) {
        if (isFiller(t)) {
            continue;
        }
        if (numeral.match(t).hasMatch() || wordValues().contains(t)) {
            nums.append(t);
        } else {
            return std::nullopt;
        }
    }
    // A real slide number is a handful of tokens; a flood is malformed (audit F1).
    if (nums.isEmpty() || nums.size() > 12) {
        return std::nullopt;
    }

    // A single multi-digit numeral is that number directly ("150" -> 150).
    if (nums.size() == 1 && numeral.match(nums.first()).hasMatch()) {
        bool ok = false;
        const int v = nums.first().toInt(&ok);
        return ok ? std::optional<int>(v) : std::nullopt;
    }

    // Map each token to its value, and note whether every token is a single
    // digit (0-9) — the marker of a digit-by-digit spelling ("one five" -> 15).
    QList<int> values;
    bool allSingleDigits = true;
    for (const QString& t : nums) {
        int v = 0;
        if (numeral.match(t).hasMatch()) {
            bool ok = false;
            v = t.toInt(&ok);
            if (!ok) {
                return std::nullopt; // numeral too large to represent (audit F2)
            }
        } else {
            v = wordValues().value(t);
        }
        values.append(v);
        if (v < 0 || v > 9) {
            allSingleDigits = false;
        }
    }

    if (allSingleDigits && values.size() >= 2) {
        QString digits;
        for (int v : values) {
            digits += QString::number(v);
        }
        bool ok = false;
        const int v = digits.toInt(&ok);
        return ok ? std::optional<int>(v) : std::nullopt;
    }

    return evalWords(values); // returns nullopt on malformed/overflow (F1/F3)
}

} // namespace pptv
