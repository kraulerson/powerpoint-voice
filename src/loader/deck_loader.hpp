#pragma once

#include <QString>

#include "model/slide_model.hpp"

// Deck loader: opens and validates an untrusted .pptx and builds the in-memory
// Presentation model (Project Bible §3/§5, threat-model TM-014..018). The .pptx
// is attacker-controlled input (arrives by email/USB), so every limit is
// enforced and the loader NEVER writes to disk — parts are read in memory.
namespace pptv {

enum class LoadErrorKind {
    None,
    FileNotFound,
    NotAZip,
    MissingPresentationPart,
    MalformedXml,
    FileTooLarge,       // compressed archive exceeds maxFileBytes
    TooManySlides,      // slide count exceeds maxSlides
    PartTooLarge,       // a single part's uncompressed size exceeds cap
    DecompressionLimit, // cumulative uncompressed size exceeds cap (zip bomb)
};

struct LoadError {
    LoadErrorKind kind = LoadErrorKind::None;
    QString message; // human-facing; names the failing part where possible
};

// Resource caps enforced during load. Defaults match Manifesto Q9 / F1.
// Injectable so tests can drive the cap paths with small fixtures.
struct LoaderLimits {
    long long maxFileBytes = 200LL * 1024 * 1024;          // 200 MB archive
    int maxSlides = 300;                                   // deck length
    long long maxTotalUncompressed = 1024LL * 1024 * 1024; // 1 GB total
    long long maxPartUncompressed = 128LL * 1024 * 1024;   // per-part
    // Per-slide element cap (audit F1a-4): a single legal-but-pathological slide
    // with millions of shapes would exhaust memory at parse time. Beyond the cap
    // the slide loads with the elements seen so far plus a warning.
    int maxShapesPerSlide = 5000;
    // Per-text-box paragraph/run caps and per-run text length (audit R5): the
    // shape cap alone does not bound text bodies — one box can carry millions of
    // paragraphs/runs or a gigabyte of text. Excess is truncated.
    int maxParagraphsPerBox = 2000;
    int maxRunsPerParagraph = 1000;
    int maxRunTextChars = 100000;
};

struct LoadResult {
    bool ok = false;
    LoadError error;
    Presentation presentation;
};

class DeckLoader {
  public:
    // Loads and validates `path` into a Presentation. On any validation failure
    // returns ok=false with a specific LoadError and leaves the app able to
    // continue. Unsupported slide elements do NOT fail the load — they become
    // LoadWarnings + placeholders.
    static LoadResult load(const QString& path, const LoaderLimits& limits = LoaderLimits{});
};

} // namespace pptv
