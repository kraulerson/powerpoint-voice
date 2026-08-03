#include <doctest/doctest.h>

#include <QByteArray>
#include <QFile>
#include <QString>

#include "loader/deck_loader.hpp"

// FIXTURES_DIR is injected by CMake and points at tests/fixtures/ in the source
// tree. The fixtures are synthetic OOXML (never the real deck).
namespace {
QString fixture(const char* name) {
    return QString::fromUtf8(FIXTURES_DIR) + QLatin1Char('/') + QLatin1String(name);
}
} // namespace

using namespace pptv;

// ---------------------------------------------------------------------------
// Happy path — text tier
// ---------------------------------------------------------------------------
TEST_CASE("loads a well-formed text deck") {
    LoadResult r = DeckLoader::load(fixture("good_text.pptx"));

    REQUIRE(r.ok);
    CHECK(r.error.kind == LoadErrorKind::None);

    SUBCASE("slide count and slide size are read") {
        CHECK(r.presentation.slides.size() == 2);
        CHECK(r.presentation.slideWidth == 12192000); // 16:9 EMU
        CHECK(r.presentation.slideHeight == 6858000);
    }

    SUBCASE("first slide's title text is extracted with properties") {
        REQUIRE(r.presentation.slides.size() >= 1);
        const Slide& s = r.presentation.slides[0];
        REQUIRE(s.elements.size() >= 1);
        const ShapeElement& e = s.elements[0];
        CHECK(e.kind == ElementKind::TextBox);
        REQUIRE(e.textBox.paragraphs.size() >= 1);
        REQUIRE(e.textBox.paragraphs[0].runs.size() >= 1);
        const TextRun& run = e.textBox.paragraphs[0].runs[0];
        CHECK(run.text == QStringLiteral("Quarterly Review"));
        CHECK(run.bold == true);
        CHECK(run.fontSizePt == doctest::Approx(44.0)); // sz=4400 -> 44pt
        CHECK(run.fontFamily == QStringLiteral("Calibri"));
    }

    SUBCASE("text box position is read in EMU") {
        const TextBox& tb = r.presentation.slides[0].elements[0].textBox;
        CHECK(tb.rect.x == 838200);
        CHECK(tb.rect.y == 365125);
        CHECK(tb.rect.cx == 10515600);
        CHECK(tb.rect.cy == 1325563);
    }

    SUBCASE("solid slide background is read") {
        const Background& bg = r.presentation.slides[0].background;
        CHECK(bg.kind == BackgroundKind::Solid);
        REQUIRE(bg.solid.has_value());
        CHECK(int(bg.solid->r) == 0x1E);
        CHECK(int(bg.solid->g) == 0x24);
        CHECK(int(bg.solid->b) == 0x30);
    }

    SUBCASE("no warnings for a fully-supported deck") {
        CHECK(r.presentation.warnings.empty());
    }
}

// ---------------------------------------------------------------------------
// Happy path — image element (pixels NOT decoded here; ref resolved)
// ---------------------------------------------------------------------------
TEST_CASE("resolves an embedded image to its media part") {
    LoadResult r = DeckLoader::load(fixture("good_image.pptx"));
    REQUIRE(r.ok);
    REQUIRE(r.presentation.slides.size() == 1);

    const Slide& s = r.presentation.slides[0];
    bool found_image = false;
    for (const ShapeElement& e : s.elements) {
        if (e.kind == ElementKind::Image) {
            found_image = true;
            CHECK(e.image.mediaPart == QStringLiteral("ppt/media/image1.png"));
            CHECK(e.image.rect.cx == 2000000);
            // F1b: the raw (still-encoded) image bytes are loaded for the renderer,
            // and they are a real PNG (starts with the PNG signature).
            CHECK_FALSE(e.image.imageData.isEmpty());
            CHECK(e.image.imageData.startsWith("\x89PNG"));
        }
    }
    CHECK(found_image);
}

// ---------------------------------------------------------------------------
// Unsupported element → warning + placeholder, NOT a failed load
// ---------------------------------------------------------------------------
TEST_CASE("records a warning for an unsupported element but still loads") {
    LoadResult r = DeckLoader::load(fixture("good_unsupported.pptx"));

    REQUIRE(r.ok); // an unsupported element must NOT fail the whole load
    REQUIRE(r.presentation.slides.size() == 1);

    CHECK_FALSE(r.presentation.warnings.empty());
    CHECK(r.presentation.warnings[0].slideIndex == 1);
    CHECK(r.presentation.warnings[0].elementType.contains(QStringLiteral("table"),
                                                          Qt::CaseInsensitive));

    // The supported text box on the same slide is still extracted.
    bool has_text = false;
    for (const ShapeElement& e : r.presentation.slides[0].elements) {
        if (e.kind == ElementKind::TextBox) {
            has_text = true;
        }
    }
    CHECK(has_text);
}

// ---------------------------------------------------------------------------
// Error paths — each produces a specific, non-crashing LoadError
// ---------------------------------------------------------------------------
TEST_CASE("rejects a file that is not a zip") {
    LoadResult r = DeckLoader::load(fixture("bad_notzip.pptx"));
    CHECK_FALSE(r.ok);
    CHECK(r.error.kind == LoadErrorKind::NotAZip);
}

TEST_CASE("rejects a zip missing the presentation part") {
    LoadResult r = DeckLoader::load(fixture("bad_nopresentation.pptx"));
    CHECK_FALSE(r.ok);
    CHECK(r.error.kind == LoadErrorKind::MissingPresentationPart);
}

TEST_CASE("rejects malformed presentation XML") {
    LoadResult r = DeckLoader::load(fixture("bad_malformedxml.pptx"));
    CHECK_FALSE(r.ok);
    CHECK(r.error.kind == LoadErrorKind::MalformedXml);
}

TEST_CASE("rejects a missing file") {
    LoadResult r = DeckLoader::load(fixture("does_not_exist.pptx"));
    CHECK_FALSE(r.ok);
    CHECK(r.error.kind == LoadErrorKind::FileNotFound);
}

// ---------------------------------------------------------------------------
// Security caps (threat model TM-014..018) — driven with small limits so a
// tiny fixture exercises the same code paths a hostile 1 GB deck would hit.
// ---------------------------------------------------------------------------
TEST_CASE("enforces the slide-count cap") {
    LoaderLimits limits;
    limits.maxSlides = 1; // good_text has 2 slides
    LoadResult r = DeckLoader::load(fixture("good_text.pptx"), limits);
    CHECK_FALSE(r.ok);
    CHECK(r.error.kind == LoadErrorKind::TooManySlides);
}

TEST_CASE("enforces the cumulative decompression cap (zip-bomb defense)") {
    LoaderLimits limits;
    limits.maxTotalUncompressed = 64; // any real deck exceeds this
    LoadResult r = DeckLoader::load(fixture("good_text.pptx"), limits);
    CHECK_FALSE(r.ok);
    CHECK(r.error.kind == LoadErrorKind::DecompressionLimit);
}

// ===========================================================================
// ORCHESTRATOR ASSERTIONS (Build Loop Step 1) — chosen by Karl Raulerson at the
// 2026-08-03 test gate. Each maps to a stated requirement; the AI wrote the C++
// to Karl's direction (Solo Orchestrator model: he directs the WHAT).
// ===========================================================================

// Karl #1 — slide order is preserved (correctness of "go to slide N").
TEST_CASE("orchestrator: slide order matches the deck's declared order") {
    LoadResult r = DeckLoader::load(fixture("good_text.pptx"));
    REQUIRE(r.ok);
    REQUIRE(r.presentation.slides.size() == 2);
    // slide1 declares "Quarterly Review", slide2 declares "Agenda".
    CHECK(r.presentation.slides[0].elements[0].textBox.paragraphs[0].runs[0].text ==
          QStringLiteral("Quarterly Review"));
    CHECK(r.presentation.slides[1].elements[0].textBox.paragraphs[0].runs[0].text ==
          QStringLiteral("Agenda"));
}

// Karl #2 — every slide's text is extracted, not just slide 1.
TEST_CASE("orchestrator: text from the second slide is extracted") {
    LoadResult r = DeckLoader::load(fixture("good_text.pptx"));
    REQUIRE(r.ok);
    REQUIRE(r.presentation.slides.size() >= 2);
    const Slide& s2 = r.presentation.slides[1];
    REQUIRE(s2.elements.size() >= 1);
    CHECK(s2.elements[0].textBox.paragraphs[0].runs[0].text == QStringLiteral("Agenda"));
}

// Karl #3 — loading never modifies the file on disk (Confidential safety;
// no-write invariant, threat model).
TEST_CASE("orchestrator: loading does not modify the .pptx on disk") {
    const QString path = fixture("good_text.pptx");
    QFile f(path);
    REQUIRE(f.open(QIODevice::ReadOnly));
    const QByteArray before = f.readAll();
    f.close();

    LoadResult r = DeckLoader::load(path);
    REQUIRE(r.ok);

    REQUIRE(f.open(QIODevice::ReadOnly));
    const QByteArray after = f.readAll();
    f.close();
    CHECK(before == after); // byte-for-byte unchanged
}

// Karl #4 — re-loading the same deck is deterministic (pre-render cache and
// "go to slide N" both rely on stable output).
TEST_CASE("orchestrator: re-loading yields identical results") {
    const QString path = fixture("good_text.pptx");
    LoadResult a = DeckLoader::load(path);
    LoadResult b = DeckLoader::load(path);
    REQUIRE(a.ok);
    REQUIRE(b.ok);
    CHECK(a.presentation.slides.size() == b.presentation.slides.size());
    CHECK(a.presentation.slideWidth == b.presentation.slideWidth);
    const TextRun& ra = a.presentation.slides[0].elements[0].textBox.paragraphs[0].runs[0];
    const TextRun& rb = b.presentation.slides[0].elements[0].textBox.paragraphs[0].runs[0];
    CHECK(ra.text == rb.text);
    CHECK(ra.fontSizePt == doctest::Approx(rb.fontSizePt));
    CHECK(a.presentation.slides[0].elements[0].textBox.rect.x ==
          b.presentation.slides[0].elements[0].textBox.rect.x);
}

// Karl #5 — a slide with no declared background is None, never a wrong black
// fill mistaken for content.
TEST_CASE("orchestrator: absent background reads as None, not a fabricated color") {
    LoadResult r = DeckLoader::load(fixture("good_text.pptx"));
    REQUIRE(r.ok);
    REQUIRE(r.presentation.slides.size() == 2);
    // slide2 declares no <p:bg>.
    CHECK(r.presentation.slides[1].background.kind == BackgroundKind::None);
    CHECK_FALSE(r.presentation.slides[1].background.solid.has_value());
}

// Karl #6 — an empty text box loads without crashing and invents no phantom run.
TEST_CASE("orchestrator: an empty text box does not crash or fabricate text") {
    LoadResult r = DeckLoader::load(fixture("good_edge.pptx"));
    REQUIRE(r.ok);
    REQUIRE(r.presentation.slides.size() == 1);
    // Exactly one run with non-empty text ("Present") across the whole slide.
    int nonempty_runs = 0;
    for (const ShapeElement& e : r.presentation.slides[0].elements) {
        if (e.kind != ElementKind::TextBox) {
            continue;
        }
        for (const Paragraph& p : e.textBox.paragraphs) {
            for (const TextRun& run : p.runs) {
                if (!run.text.isEmpty()) {
                    ++nonempty_runs;
                }
            }
        }
    }
    CHECK(nonempty_runs == 1);
}

// Karl #7 — element count is exact: no dropped, no duplicated elements.
TEST_CASE("orchestrator: a text+image slide yields exactly two elements") {
    LoadResult r = DeckLoader::load(fixture("good_image.pptx"));
    REQUIRE(r.ok);
    REQUIRE(r.presentation.slides.size() == 1);
    CHECK(r.presentation.slides[0].elements.size() == 2);
}

// ===========================================================================
// SECURITY-AUDIT REMEDIATION (Build Loop Step 4) — regression tests for the
// findings the 5 parallel audit agents confirmed on 2026-08-03.
// ===========================================================================

// F1a-2 — deeply nested hostile XML must not crash (was recursive stack
// overflow; walker is now iterative). If this test hangs/crashes, the fix
// regressed.
TEST_CASE("audit F1a-2: deeply nested XML loads without crashing") {
    LoadResult r = DeckLoader::load(fixture("deep_nest.pptx"));
    CHECK(r.ok); // it parses; the deep <a:x> nest is simply not a solidFill it uses
    CHECK(r.presentation.slides.size() == 1);
}

// F1a-3 — a missing slide RELATIONSHIP must preserve slide numbering with a
// placeholder, never silently drop (which would misdirect "go to slide N").
TEST_CASE("audit F1a-3: missing slide relationship preserves index via placeholder") {
    LoadResult r = DeckLoader::load(fixture("drop_missing_rel.pptx"));
    REQUIRE(r.ok);
    REQUIRE(r.presentation.slides.size() == 3); // NOT 2
    CHECK(r.presentation.slides[1].placeholder == true);
    // Slides 1 and 3 still carry their real text at the correct indices.
    CHECK(r.presentation.slides[0].elements[0].textBox.paragraphs[0].runs[0].text ==
          QStringLiteral("One"));
    CHECK(r.presentation.slides[2].elements[0].textBox.paragraphs[0].runs[0].text ==
          QStringLiteral("Three"));
    // The drop is surfaced, not silent.
    CHECK_FALSE(r.presentation.warnings.empty());
}

// F1a-3 — same guarantee when the slide PART is missing.
TEST_CASE("audit F1a-3: missing slide part preserves index via placeholder") {
    LoadResult r = DeckLoader::load(fixture("drop_missing_part.pptx"));
    REQUIRE(r.ok);
    REQUIRE(r.presentation.slides.size() == 3);
    CHECK(r.presentation.slides[1].placeholder == true);
    CHECK(r.presentation.slides[2].elements[0].textBox.paragraphs[0].runs[0].text ==
          QStringLiteral("Three"));
}

// F1a-4 — a slide with more shapes than the per-slide cap loads (capped) with a
// warning, instead of exhausting memory.
TEST_CASE("audit F1a-4: per-slide shape cap is enforced with a warning") {
    LoaderLimits limits;
    limits.maxShapesPerSlide = 50; // fixture has 200
    LoadResult r = DeckLoader::load(fixture("many_shapes.pptx"), limits);
    REQUIRE(r.ok);
    REQUIRE(r.presentation.slides.size() == 1);
    CHECK(r.presentation.slides[0].elements.size() == 50);
    CHECK_FALSE(r.presentation.warnings.empty());
}
