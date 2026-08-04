#pragma once

#include <optional>

#include <QString>

// Voice-command grammar matcher (Features F2 voice-nav + F3 recognition-control).
// The recognizer (and, later, the keyboard) produce text; this maps a phrase to
// one of the five closed-grammar commands, or nullopt when the phrase is not a
// command. Matching is PHRASE-LEVEL, not substring: an audience sentence that
// merely CONTAINS "next" must not fire NextSlide. This is the front line of the
// audience-false-trigger defense (threat model TM-002/019).
namespace pptv {

// The five-value closed command grammar (Project Bible §5 data model).
enum class CommandType {
    NextSlide,
    PreviousSlide,
    PausePresentation,
    ContinuePresentation,
    GoToSlide,
};

struct Command {
    CommandType type;
    int slideNumber = 0; // meaningful only when type == GoToSlide; 0 otherwise
};

// Maps a recognized phrase to a Command, or nullopt if the phrase is not one of
// the five commands. "go to slide N" reuses parseSlideNumber for the number; a
// "go to slide" with no parseable number yields nullopt (no jump). The number is
// NOT range-checked here — the caller (F7) compares it to the deck length.
std::optional<Command> matchCommand(const QString& phrase);

} // namespace pptv
