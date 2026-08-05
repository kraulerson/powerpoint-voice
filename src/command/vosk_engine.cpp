#include "command/vosk_engine.hpp"

#include <limits>

#include <QJsonDocument>
#include <QJsonObject>

#include "audio/audio_format.hpp"

#include "vosk_api.h"

namespace pptv {

QString textFromVoskResult(const std::string& json) {
    // Vosk returns a JSON document. Only "text" is read: confidence, alternatives
    // and timings are deliberately ignored, and nothing here is logged or stored —
    // heard speech must not reach a log or the projector (Bible §8, TM-012/013).
    const QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(json));
    if (!doc.isObject()) {
        return {};
    }
    return doc.object().value(QStringLiteral("text")).toString().trimmed();
}

std::vector<QString> unknownGrammarWords(const std::vector<QString>& phrases,
                                         const WordLookup& modelKnowsWord) {
    std::vector<QString> unknown;
    if (!modelKnowsWord) {
        return unknown;
    }
    for (const QString& phrase : phrases) {
        for (const QString& word : phrase.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
            if (modelKnowsWord(word)) {
                continue;
            }
            bool already = false;
            for (const QString& u : unknown) {
                if (u == word) {
                    already = true;
                }
            }
            if (!already) {
                unknown.push_back(word);
            }
        }
    }
    return unknown;
}

struct VoskEngine::Impl {
    VoskModel* model = nullptr;
    VoskRecognizer* rec = nullptr;
};

VoskEngine::VoskEngine() : d_(std::make_unique<Impl>()) {
    // Vosk logs decoder internals to stderr at the default level, which on this
    // project is a disclosure channel: it would print heard words. Silence it before
    // anything is loaded.
    vosk_set_log_level(-1);
}

VoskEngine::~VoskEngine() {
    stop();
}

RecognizerInitError VoskEngine::start(const RecognizerSetup& setup) {
    if (setup.error != RecognizerInitError::None) {
        return setup.error;
    }
    stop();
    d_->model = vosk_model_new(setup.modelDir.toUtf8().constData());
    if (d_->model == nullptr) {
        return RecognizerInitError::ModelMissing;
    }
    // BUG-65: Vosk DROPS grammar tokens the model does not know, silently, which
    // widens what can be recognised. Verify every word round-trips before trusting
    // the grammar to constrain anything.
    const auto unknown = unknownGrammarWords(grammarPhrases(), [this](const QString& w) {
        return vosk_model_find_word(d_->model, w.toUtf8().constData()) >= 0;
    });
    if (!unknown.empty()) {
        vosk_model_free(d_->model);
        d_->model = nullptr;
        return RecognizerInitError::GrammarRejected;
    }
    d_->rec = vosk_recognizer_new_grm(d_->model, static_cast<float>(kRecognizerSampleRate),
                                      setup.grammar.c_str());
    if (d_->rec == nullptr) {
        vosk_model_free(d_->model);
        d_->model = nullptr;
        return RecognizerInitError::EngineUnavailable;
    }
    return RecognizerInitError::None;
}

void VoskEngine::stop() {
    if (d_->rec != nullptr) {
        vosk_recognizer_free(d_->rec);
        d_->rec = nullptr;
    }
    if (d_->model != nullptr) {
        vosk_model_free(d_->model);
        d_->model = nullptr;
    }
}

bool VoskEngine::isRunning() const {
    return d_->rec != nullptr;
}

QString VoskEngine::feed(const std::int16_t* samples, std::size_t count) {
    if (d_->rec == nullptr || samples == nullptr || count == 0) {
        return {};
    }
    // vosk takes an int length; refuse a buffer that would not fit rather than
    // truncating silently.
    if (count > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return {};
    }
    if (vosk_recognizer_accept_waveform_s(d_->rec, samples, static_cast<int>(count)) == 0) {
        return {}; // mid-utterance
    }
    const char* json = vosk_recognizer_result(d_->rec);
    return json == nullptr ? QString() : textFromVoskResult(json);
}

} // namespace pptv
