#pragma once

#include <optional>
#include <vector>

#include <QString>

// In-memory domain model produced by the deck loader (Project Bible §5,
// docs/phase-1/data-model.md). GUI-free and render-free: this is the parsed
// representation of a .pptx, decoupled from how it is later painted (F1b).
namespace pptv {

// English Metric Units — OOXML's coordinate unit. 914400 EMU per inch.
using Emu = long long;

struct Color {
    unsigned char r = 0;
    unsigned char g = 0;
    unsigned char b = 0;
    unsigned char a = 255;
};

// A positioned rectangle in slide coordinates (EMU).
struct RectEmu {
    Emu x = 0;
    Emu y = 0;
    Emu cx = 0;
    Emu cy = 0;
};

// A run of text sharing one set of character properties. Unspecified properties
// (empty family, zero size, nullopt color) mean "inherit" — resolved at render.
struct TextRun {
    QString text;
    QString fontFamily;
    double fontSizePt = 0.0;
    bool bold = false;
    bool italic = false;
    std::optional<Color> color;
};

struct Paragraph {
    std::vector<TextRun> runs;
};

struct TextBox {
    RectEmu rect;
    std::vector<Paragraph> paragraphs;
};

// A picture placed on a slide. The pixels are NOT decoded here — the loader
// resolves the relationship to the media part path; decoding is the render
// layer's job (F1b), keeping the parse layer free of image codecs.
struct ImageElement {
    RectEmu rect;
    QString mediaPart; // e.g. "ppt/media/image1.png"
};

enum class ElementKind { TextBox, Image };

// A single z-ordered element on a slide. A tagged struct (rather than a variant)
// keeps test assertions and field access straightforward.
struct ShapeElement {
    ElementKind kind = ElementKind::TextBox;
    TextBox textBox;
    ImageElement image;
};

enum class BackgroundKind { None, Solid, Picture };

struct Background {
    BackgroundKind kind = BackgroundKind::None;
    std::optional<Color> solid;
    QString pictureMediaPart;
};

// Records an element the text+images tier does not render, so F1's warning list
// is data-driven (Manifesto F1: "visible placeholder + warning list, never a
// silent wrong render").
struct LoadWarning {
    int slideIndex = 0; // 1-based
    QString elementType;
    QString detail;
};

struct Slide {
    Background background;
    std::vector<ShapeElement> elements; // vector order == z-order
    std::vector<LoadWarning> warnings;
    // True when the slide's part could not be read/resolved. A placeholder is
    // inserted (rather than dropping the slide) so that slide N in the deck
    // always maps to slides[N-1] — "go to slide N" must never drift (audit F1a-3).
    bool placeholder = false;
};

struct Presentation {
    Emu slideWidth = 0;
    Emu slideHeight = 0;
    std::vector<Slide> slides;
    std::vector<LoadWarning> warnings; // aggregate across all slides
};

} // namespace pptv
