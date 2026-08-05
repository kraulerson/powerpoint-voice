#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <QString>

#include "command/vosk_recognizer.hpp"

// The live Vosk decoding session (Feature F8d).
//
// Separated from the grammar/precondition layer so everything above it stays
// testable on a machine with no model, and so the ONE place that links libvosk is
// this file.
namespace pptv {

// Extracts the recognised text from a Vosk result document. Vosk returns JSON;
// this pulls out the "text" field and nothing else — no confidence, no alternatives,
// no timing. Pure and testable without the library.
QString textFromVoskResult(const std::string& json);

// Words in `phrases` that the model does NOT know. Vosk drops unknown grammar
// tokens SILENTLY, which quietly widens what can be recognised (BUG-65), so the
// caller must refuse to run when this is non-empty rather than trust the grammar.
using WordLookup = std::function<bool(const QString&)>;
std::vector<QString> unknownGrammarWords(const std::vector<QString>& phrases,
                                         const WordLookup& modelKnowsWord);

// A live decoder. Owns the Vosk model and recogniser.
class VoskEngine {
  public:
    VoskEngine();
    ~VoskEngine();
    VoskEngine(const VoskEngine&) = delete;
    VoskEngine& operator=(const VoskEngine&) = delete;

    // Loads the model, verifies the grammar is fully in-vocabulary, and creates a
    // grammar-constrained recogniser. Voice stays OFF on any error.
    RecognizerInitError start(const RecognizerSetup& setup);
    void stop();
    bool isRunning() const;

    // Feeds 16 kHz mono samples. Returns a FINAL utterance when one completes,
    // otherwise an empty string. Called on the audio consumer thread.
    QString feed(const std::int16_t* samples, std::size_t count);

  private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

} // namespace pptv
