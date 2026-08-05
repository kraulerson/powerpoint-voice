#pragma once

#include <memory>
#include <string>
#include <vector>

#include <QString>

#include "command/recognizer_controller.hpp"

// Grammar-constrained speech recognition via Vosk (Feature F8c).
//
// THE SAFETY PROPERTY THIS FILE EXISTS FOR: the decoder must be able to produce
// ONLY the five commands. Not "usually produce", not "produce them best" — only.
// An unconstrained decoder is a ~200,000-word English model listening to a room
// full of executives, and threat TM-002/TM-019 is precisely that the AUDIENCE
// moves the presenter's slides.
//
// Vosk offers that as `vosk_recognizer_new_grm`, and it fails OPEN in two ways
// that produce no error at all:
//   1. a model built with a STATIC graph (graph/HCLG.fst) silently ignores the
//      grammar and decodes the full vocabulary. Only a model with a DYNAMIC graph
//      (graph/HCLr.fst + graph/Gr.fst) constrains. Verified present in the vendored
//      model — and asserted at load here, because "verified once" is not a control.
//   2. a grammar token outside the model's vocabulary is dropped silently, so a
//      typo in the grammar quietly widens what can be recognised.
// Both are checked before the recogniser is allowed to run.
namespace pptv {

// Why a recogniser could not be created. Closed vocabulary: this can reach the
// operator surface, so no path, no vendor string (Bible §8, TM-012/013).
enum class RecognizerInitError {
    None,
    ModelMissing,           // the model directory is not where it should be
    ModelNotGrammarCapable, // static graph — the grammar would be IGNORED
    GrammarRejected,        // vosk would not accept the grammar
    EngineUnavailable,      // the library could not create a recogniser
};

const char* describeRecognizerInitError(RecognizerInitError e);

// The exact phrases the decoder is permitted to emit. Built from the SAME command
// list the matcher accepts, so the two cannot drift apart.
std::vector<QString> grammarPhrases();

// The grammar as Vosk wants it: a JSON array of strings. BUILT, never interpolated
// — invalid grammar JSON SEGFAULTS vosk rather than returning an error.
std::string grammarJson(const std::vector<QString>& phrases);

// True when `dir` holds a model that can actually honour a grammar, i.e. a dynamic
// graph. A static-graph model loads fine and then ignores the constraint.
bool modelIsGrammarCapable(const QString& dir);

// Everything the recogniser needs, resolved and checked.
struct RecognizerSetup {
    RecognizerInitError error = RecognizerInitError::None;
    QString modelDir;
    std::string grammar;
};

// Resolves and validates without touching the Vosk library, so every precondition
// is testable on a machine with no model and no microphone.
RecognizerSetup prepareRecognizer(const QString& modelDir);

} // namespace pptv
