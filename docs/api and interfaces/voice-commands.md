# Interface: Voice Command Matcher & Dispatch (Features F2/F3)

**Headers:** `src/command/command_matcher.hpp`, `src/command/recognizer_controller.hpp`

## Purpose

The correctness- and safety-critical layer that decides, for each recognized phrase,
whether it is one of the app's commands and whether to act on it right now. This is
the "brain" of voice control; the speech engine + microphone (the "ears") is a
separate feature that feeds this layer through the `IRecognizer` interface.

## 1. The grammar matcher

```cpp
namespace pptv {

enum class CommandType {
    NextSlide, PreviousSlide, PausePresentation, ContinuePresentation, GoToSlide
};
struct Command { CommandType type; int slideNumber = 0; }; // slideNumber only for GoToSlide

std::optional<Command> matchCommand(const QString& phrase);

} // namespace pptv
```

Maps a phrase to a `Command`, or `std::nullopt` if the phrase is not a command.

| Input | Result |
|---|---|
| `"next slide"`, `"Next Slide"`, `"  next  slide "`, `"Next slide."` | NextSlide |
| `"previous slide"` | PreviousSlide |
| `"pause presentation"` / `"continue presentation"` | Pause / Continue |
| `"go to slide 7"`, `"go to slide fifteen"`, `"go to slide one five"` | GoToSlide(7 / 15 / 15) |

**Fails safe → nullopt** for anything else: empty, garbage (`"banana"`), a partial
command (`"next"`), a sentence that merely *contains* a keyword
(`"move to the next section"`), `"go to slide"` with no/garbled number. Matching is
**phrase-level, not substring** — an audience sentence can never forge a command
(threat model TM-002/019). The number in "go to slide N" reuses `parseSlideNumber`
(F4) and inherits its fail-safe behavior; range-checking against the deck length is
the caller's (F7's) job. Non-`GoToSlide` commands always report `slideNumber == 0`.

## 2. The dispatch controller

```cpp
class IRecognizer {                       // implemented by the future Vosk+mic adapter
    virtual void setPhraseHandler(std::function<void(const QString&)>) = 0;
    virtual void start() = 0;
    virtual void stop() = 0;
};

class RecognizerController {
    enum class State { Active, Paused };
    explicit RecognizerController(std::function<void(Command)> sink);
    void onPhrase(const QString& phrase);   // feed one recognized phrase
    State state() const;
};
```

`onPhrase` runs the phrase through `matchCommand` and dispatches at most one
`Command` to the sink, gated by a listening state:

- **Active** — nav commands (Next / Previous / GoToSlide) dispatch; `pause
  presentation` dispatches and enters **Paused**.
- **Paused** — nav commands are **dropped** (audience-Q&A false-trigger protection);
  only `continue presentation` dispatches and returns to Active.
- Pausing while paused / continuing while active are no-ops.

## Contract (from the F2/F3 security audit — binding on the voice-engine adapter)

- **Finalized phrases only:** an `IRecognizer` delivers one call per recognized
  utterance, no partial/interim results (else one command fires multiple jumps).
- **Same thread:** `onPhrase()` and `state()` must run on one thread; an audio-thread
  recognizer marshals phrases onto that thread (e.g. a Qt queued connection) —
  `state_` is non-atomic.
- **Sink safety:** the sink runs synchronously inside `onPhrase`; it owns its error
  handling, must not destroy the controller during dispatch, and must not re-enter
  `onPhrase` (re-entrancy is dropped as a backstop). Any exception it throws is
  caught so it can never cross the recognizer's call boundary.
- **No content logging:** this layer never logs or persists heard text or deck
  content; a `Command` carries only `{CommandType, int}` (Bible §8).
