#pragma once

#include <optional>

#include <QString>

// Spoken/typed slide-number parsing (Feature F4, Manifesto "go to slide N").
// The voice recognizer and keyboard both produce text; this turns the number
// part into an integer. It is the correctness-critical piece behind jumping to
// the right slide during a live talk, so its behavior is nailed down by tests.
namespace pptv {

// Parses the number in a "go to slide N" phrase (or a bare number) into an
// integer, or nullopt when there is no parseable number. Handles:
//   - digits:          "15"            -> 15
//   - number words:    "fifteen"       -> 15
//                      "twenty three"  -> 23
//                      "one hundred twenty three" -> 123
//   - digit-by-digit:  "one five"      -> 15   (each token a single digit)
// Case-insensitive; ignores leading filler ("go", "to", "slide", "number", …).
// Range-checking against the deck length is the caller's responsibility.
std::optional<int> parseSlideNumber(const QString& spoken);

} // namespace pptv
