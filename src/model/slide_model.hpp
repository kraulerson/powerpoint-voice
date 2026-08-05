#pragma once

#include <optional>
#include <vector>

#include <QByteArray>
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
    // Bullet marker to prefix (empty = no bullet) and list indent level
    // (0 = top level). Populated from <a:pPr> (BUG-7).
    QString bulletChar;
    int indentLevel = 0;
};

struct TextBox {
    RectEmu rect;
    std::vector<Paragraph> paragraphs;
};

// A picture placed on a slide. The loader resolves the relationship to the
// media part path and loads its raw (still-encoded) bytes; the render layer
// (F1b) decodes them with QImage. Decoding is deliberately NOT done in the
// parse layer, keeping image-codec exposure out of the untrusted-XML walk.
// A <a:srcRect> source crop, in DrawingML's units: thousandths of a percent of the
// image edge, inset from each side. l=29178 means "discard the leftmost 29.178%".
// Values may be NEGATIVE, which pads rather than crops.
struct SrcRect {
    int leftPerMille = 0; // actually per-100000; kept as the raw OOXML value
    int topPerMille = 0;
    int rightPerMille = 0;
    int bottomPerMille = 0;

    bool isIdentity() const {
        return leftPerMille == 0 && topPerMille == 0 && rightPerMille == 0 && bottomPerMille == 0;
    }
};

struct ImageElement {
    RectEmu rect;
    QString mediaPart;    // e.g. "ppt/media/image1.png"
    QByteArray imageData; // raw encoded bytes (PNG/JPEG/...); decoded at render
    // The part of the source image the deck actually shows (BUG-37). Ignoring this
    // draws margins PowerPoint crops away — on the real deck a picture cropped 29.178%
    // off EACH side was being drawn whole, so its white surround appeared on a dark
    // slide as a box around the artwork.
    SrcRect srcRect;
    // <a:alphaModFix amt="..."> — uniform picture opacity, per-100000. 100000 = opaque.
    int alphaPerMille = 100000;
};

// An element the text+images tier cannot render faithfully (table/chart/SmartArt).
// It keeps its geometry so the renderer can draw a VISIBLE placeholder box in the
// right spot — the Manifesto's "visible placeholder, never a silent wrong render".
struct UnsupportedElement {
    RectEmu rect;
    QString type; // "table" / "chart" / "smartArt" / raw tag
};

enum class ElementKind { TextBox, Image, Unsupported };

// A single z-ordered element on a slide. A tagged struct (rather than a variant)
// keeps test assertions and field access straightforward.
struct ShapeElement {
    ElementKind kind = ElementKind::TextBox;
    TextBox textBox;
    ImageElement image;
    UnsupportedElement unsupported;
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
