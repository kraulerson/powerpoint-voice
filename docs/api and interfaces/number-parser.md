# Interface: parseSlideNumber (Feature F4)

**Header:** `src/command/number_parser.hpp`

## Purpose

Turn the number in a "go to slide N" command (spoken via the recognizer, or typed) into an
integer. This is the correctness-critical step behind jumping to the right slide during a live
talk.

## Entry point

```cpp
std::optional<int> pptv::parseSlideNumber(const QString& spoken);
```

Returns the parsed integer, or `std::nullopt` when there is no confidently-parseable number.

## Accepted forms

| Input | Result |
|---|---|
| `"15"`, `"go to slide 15"` | 15 |
| `"fifteen"`, `"go to slide fifteen"` | 15 |
| `"twenty three"`, `"Twenty-Three"` | 23 |
| `"one hundred twenty three"` | 123 |
| `"one five"` (digit-by-digit) | 15 |
| `"zero"` | 0 (caller rejects as out-of-range) |

Case-insensitive; tolerant of extra whitespace, hyphens, and filler words ("go", "to",
"slide", "number", "page", …).

## Fails safe (returns nullopt)

- No number present: `""`, `"go to slide"`, `"next slide"`, `"banana"`.
- Malformed / stutter sequences: `"fifteen fifteen"`, `"one ten"`, `"twenty thirty"` — rejected
  rather than summed into a wrong slide.
- Overflow / flood: an absurd count of tokens or a numeral too large to represent.

The design rule: **when unsure, jump nowhere.** A mis-heard command produces no movement plus
"try again" feedback, never a wrong slide.

## Caller responsibilities

Range-checking against the deck length is NOT done here — the command layer (F2/F7) compares
the parsed number against the slide count and shows "deck has N slides" when out of range.
