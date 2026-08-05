#include <doctest/doctest.h>

#include <algorithm>

#include <QColor>
#include <QImage>

#include "loader/deck_loader.hpp"
#include "render/slide_renderer.hpp"

namespace {
QString fixture(const char* name) {
    return QString::fromUtf8(FIXTURES_DIR) + QLatin1Char('/') + QLatin1String(name);
}

// Is any pixel inside [x0,x1) x [y0,y1) different from `bg`? Used to assert that
// something (text/image) was actually drawn in a region.
bool regionHasNonBackground(const QImage& img, int x0, int y0, int x1, int y1, QRgb bg) {
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            if (img.pixel(x, y) != bg) {
                return true;
            }
        }
    }
    return false;
}
} // namespace

using namespace pptv;

// A 16:9 target matches the fixtures' 12192000 x 6858000 EMU slide size, so no
// letterbox — the slide fills the image.
static constexpr int W = 1280;
static constexpr int H = 720;

TEST_CASE("renders the requested image dimensions") {
    LoadResult r = DeckLoader::load(fixture("good_text.pptx"));
    REQUIRE(r.ok);
    QImage img = SlideRenderer::render(r.presentation, 0, W, H);
    CHECK(img.width() == W);
    CHECK(img.height() == H);
    CHECK_FALSE(img.isNull());
}

TEST_CASE("fills a solid slide background with its declared color") {
    LoadResult r = DeckLoader::load(fixture("good_text.pptx"));
    REQUIRE(r.ok);
    // slide 1 background is #1E2430.
    QImage img = SlideRenderer::render(r.presentation, 0, W, H);
    // Sample a corner region that no text box covers.
    const QColor c = img.pixelColor(20, 20);
    CHECK(c.red() == 0x1E);
    CHECK(c.green() == 0x24);
    CHECK(c.blue() == 0x30);
}

TEST_CASE("an absent background renders white (PowerPoint default), not black") {
    LoadResult r = DeckLoader::load(fixture("good_text.pptx"));
    REQUIRE(r.ok);
    // slide 2 declares no background.
    QImage img = SlideRenderer::render(r.presentation, 1, W, H);
    const QColor c = img.pixelColor(20, 20);
    CHECK(c.red() == 0xFF);
    CHECK(c.green() == 0xFF);
    CHECK(c.blue() == 0xFF);
}

TEST_CASE("renders slide text within its text box") {
    LoadResult r = DeckLoader::load(fixture("good_text.pptx"));
    REQUIRE(r.ok);
    QImage img = SlideRenderer::render(r.presentation, 0, W, H);
    // The title text box is roughly the upper band of the slide (EMU off y ~365k
    // of 6858k -> ~5% down). Some text pixel must differ from the #1E2430 bg.
    const QRgb bg = qRgb(0x1E, 0x24, 0x30);
    CHECK(regionHasNonBackground(img, 80, 30, 1200, 220, bg));
}

TEST_CASE("renders an embedded image over the background") {
    LoadResult r = DeckLoader::load(fixture("good_image.pptx"));
    REQUIRE(r.ok);
    QImage img = SlideRenderer::render(r.presentation, 0, W, H);
    // good_image's pic is a 1x1 red PNG placed at EMU off (1e6,1.5e6) ext (2e6,1.5e6).
    // In a 1280x720 render that maps to roughly x[105..315], y[157..315]. That
    // region must contain reddish pixels, distinct from the white default bg.
    bool found_red = false;
    for (int y = 170; y < 300 && !found_red; ++y) {
        for (int x = 120; x < 300 && !found_red; ++x) {
            const QColor c = img.pixelColor(x, y);
            if (c.red() > 150 && c.green() < 100 && c.blue() < 100) {
                found_red = true;
            }
        }
    }
    CHECK(found_red);
}

TEST_CASE("a placeholder slide renders a dark surface, not white") {
    Slide ph;
    ph.placeholder = true;
    QImage img = SlideRenderer::render(ph, 12192000, 6858000, W, H);
    // Sample a corner of the slide, away from the centered "Slide unavailable"
    // text (whose glyph coverage at the exact center is font-dependent).
    const QColor c = img.pixelColor(60, 60);
    // Dark placeholder (well below mid-gray), never the white default.
    CHECK(c.red() < 60);
    CHECK(c.green() < 60);
    CHECK(c.blue() < 60);
}

TEST_CASE("a non-positive slide size does not crash and returns the requested size") {
    Slide s; // empty, no background
    QImage img = SlideRenderer::render(s, 0, 0, W, H);
    CHECK(img.width() == W);
    CHECK(img.height() == H);
    CHECK_FALSE(img.isNull());
}

TEST_CASE("mismatched aspect letterboxes with black bars") {
    LoadResult r = DeckLoader::load(fixture("good_text.pptx"));
    REQUIRE(r.ok);
    // A square target for a 16:9 slide -> black bars top and bottom.
    QImage img = SlideRenderer::render(r.presentation, 0, 720, 720);
    const QColor top = img.pixelColor(360, 2);
    CHECK(top.red() == 0);
    CHECK(top.green() == 0);
    CHECK(top.blue() == 0);
    // The centre still shows the slide background.
    const QColor mid = img.pixelColor(360, 360);
    CHECK(mid.red() == 0x1E);
}

namespace {
// Count pixels in a region that differ from `bg`.
int countNonBackground(const QImage& img, int x0, int y0, int x1, int y1, QRgb bg) {
    int n = 0;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            if (img.pixel(x, y) != bg) {
                ++n;
            }
        }
    }
    return n;
}
} // namespace

// ===========================================================================
// ORCHESTRATOR ASSERTIONS (Build Loop Step 1) — chosen by Karl Raulerson at the
// 2026-08-03 F1b test gate. AI wrote the C++ to Karl's direction.
// ===========================================================================

// Karl #1 — text color is honored (white run renders white, not default).
TEST_CASE("orchestrator: text is drawn in its declared color") {
    LoadResult r = DeckLoader::load(fixture("good_text.pptx"));
    REQUIRE(r.ok);
    QImage img = SlideRenderer::render(r.presentation, 0, W, H);
    // The title run is white on #1E2430. Some pixel in the title band must be
    // near-white (all channels high).
    bool found_white = false;
    for (int y = 30; y < 220 && !found_white; ++y) {
        for (int x = 80; x < 1200 && !found_white; ++x) {
            const QColor c = img.pixelColor(x, y);
            if (c.red() > 220 && c.green() > 220 && c.blue() > 220) {
                found_white = true;
            }
        }
    }
    CHECK(found_white);
}

// Karl #2 — rendering is deterministic (identical output for identical input).
TEST_CASE("orchestrator: rendering the same slide twice is byte-identical") {
    LoadResult r = DeckLoader::load(fixture("good_text.pptx"));
    REQUIRE(r.ok);
    QImage a = SlideRenderer::render(r.presentation, 0, W, H);
    QImage b = SlideRenderer::render(r.presentation, 0, W, H);
    CHECK(a == b);
}

// Karl #3 — font size is honored (a larger point size paints more text).
TEST_CASE("orchestrator: a larger font paints more text pixels") {
    LoadResult r = DeckLoader::load(fixture("good_fontsizes.pptx"));
    REQUIRE(r.ok);
    REQUIRE(r.presentation.slides.size() == 2);
    const QRgb black = qRgb(0, 0, 0);
    QImage big = SlideRenderer::render(r.presentation, 0, W, H);   // 60pt
    QImage small = SlideRenderer::render(r.presentation, 1, W, H); // 20pt
    const int bigPx = countNonBackground(big, 0, 0, W, H, black);
    const int smallPx = countNonBackground(small, 0, 0, W, H, black);
    CHECK(bigPx > smallPx);
    CHECK(smallPx > 0); // small text still rendered something
}

// Karl #4 — a top-positioned title paints in the top band, not the bottom.
TEST_CASE("orchestrator: top-positioned text renders in the top region") {
    LoadResult r = DeckLoader::load(fixture("good_text.pptx"));
    REQUIRE(r.ok);
    QImage img = SlideRenderer::render(r.presentation, 0, W, H);
    const QRgb bg = qRgb(0x1E, 0x24, 0x30);
    const int topPx = countNonBackground(img, 80, 30, 1200, 220, bg);
    const int bottomPx = countNonBackground(img, 80, 520, 1200, 700, bg);
    CHECK(topPx > 0);     // title is up top
    CHECK(bottomPx == 0); // nothing painted at the bottom
}

// Karl #5 — an unsupported element renders a VISIBLE marker (never silent).
TEST_CASE("orchestrator: an unsupported element renders a visible placeholder") {
    LoadResult r = DeckLoader::load(fixture("good_unsupported.pptx"));
    REQUIRE(r.ok);
    QImage img = SlideRenderer::render(r.presentation, 0, W, H);
    // The table is at EMU off(2e6,3e6) ext(6e6,2e6) on a 12.192e6 x 6.858e6 slide.
    // -> x ~[210..840], y ~[315..525] in a 1280x720 render. White default bg;
    // the placeholder marker must paint non-white pixels there.
    const QRgb white = qRgb(0xFF, 0xFF, 0xFF);
    CHECK(countNonBackground(img, 220, 320, 830, 520, white) > 0);
}

// Karl #6 — a missing image renders a visible placeholder, not a crash/blank.
TEST_CASE("orchestrator: a missing image renders a visible placeholder") {
    LoadResult r = DeckLoader::load(fixture("good_missing_image.pptx"));
    REQUIRE(r.ok);
    // imageData is empty (the media part is absent).
    QImage img = SlideRenderer::render(r.presentation, 0, W, H);
    // pic at off(3e6,2e6) ext(4e6,3e6) -> x ~[315..735], y ~[210..525]. White bg;
    // a visible "missing image" marker must appear there.
    const QRgb white = qRgb(0xFF, 0xFF, 0xFF);
    CHECK(countNonBackground(img, 320, 215, 730, 520, white) > 0);
}

// Karl #7 — two different slides never produce the same image.
TEST_CASE("orchestrator: different slides render to different images") {
    LoadResult r = DeckLoader::load(fixture("good_text.pptx"));
    REQUIRE(r.ok);
    REQUIRE(r.presentation.slides.size() == 2);
    QImage s0 = SlideRenderer::render(r.presentation, 0, W, H);
    QImage s1 = SlideRenderer::render(r.presentation, 1, W, H);
    CHECK(s0 != s1);
}

// ===========================================================================
// SECURITY-AUDIT REMEDIATION (Build Loop Step 4) — F1b render audit, 2026-08-03.
// ===========================================================================

// R1 — an absurd declared font size must render without hanging/OOM and produce
// a valid image. (If the clamp regresses, this test hangs.)
TEST_CASE("audit R1: an enormous font size renders safely, bounded") {
    LoadResult r = DeckLoader::load(fixture("good_hugefont.pptx"));
    REQUIRE(r.ok);
    QImage img = SlideRenderer::render(r.presentation, 0, W, H);
    CHECK(img.width() == W);
    CHECK_FALSE(img.isNull());
}

// R2 — an image in a non-allow-listed format (GIF behind a .png name) must NOT
// be decoded by the codec; it renders the missing-image placeholder.
TEST_CASE("audit R2: a disallowed image format renders a placeholder, not decoded") {
    LoadResult r = DeckLoader::load(fixture("good_gif_image.pptx"));
    REQUIRE(r.ok);
    QImage img = SlideRenderer::render(r.presentation, 0, W, H);
    // The image sits at off(3e6,2e6) ext(4e6,3e6). The placeholder (grey box) must
    // appear there — and specifically NOT the GIF's red pixel.
    const QRgb white = qRgb(0xFF, 0xFF, 0xFF);
    CHECK(countNonBackground(img, 320, 215, 730, 520, white) > 0);
    bool found_red = false;
    for (int y = 215; y < 520 && !found_red; ++y) {
        for (int x = 320; x < 730 && !found_red; ++x) {
            const QColor c = img.pixelColor(x, y);
            if (c.red() > 150 && c.green() < 80 && c.blue() < 80) {
                found_red = true;
            }
        }
    }
    CHECK_FALSE(found_red); // the GIF decoder was never invoked
}

// R4 — content positioned outside the slide must be clipped, never bleeding into
// the black letterbox bars.
TEST_CASE("audit R4: off-slide content is clipped out of the letterbox") {
    LoadResult r = DeckLoader::load(fixture("good_overflow.pptx"));
    REQUIRE(r.ok);
    // Square target for a 16:9 slide -> top/bottom letterbox. The unsupported box
    // is at negative EMU, so unclipped it would paint into the top-left letterbox.
    QImage img = SlideRenderer::render(r.presentation, 0, 720, 720);
    const QColor topLeft = img.pixelColor(60, 5); // inside the top letterbox band
    CHECK(topLeft.red() == 0);
    CHECK(topLeft.green() == 0);
    CHECK(topLeft.blue() == 0);
}

// R5 — a text box with more paragraphs than the cap loads (truncated) rather than
// building an unbounded model.
TEST_CASE("audit R5: per-box paragraph count is capped") {
    LoaderLimits limits;
    limits.maxParagraphsPerBox = 50; // fixture has 500
    LoadResult r = DeckLoader::load(fixture("good_manypara.pptx"), limits);
    REQUIRE(r.ok);
    REQUIRE(r.presentation.slides.size() == 1);
    REQUIRE(r.presentation.slides[0].elements.size() >= 1);
    CHECK(r.presentation.slides[0].elements[0].textBox.paragraphs.size() == 50);
}

// ===========================================================================
// UAT SESSION 1 REMEDIATION — renderer real-deck fidelity (BUG-1/4/5).
// ===========================================================================

// BUG-1 — a run with NO resolvable color renders READABLE (white) on a dark
// background, never black-on-dark (invisible).
TEST_CASE("BUG-1: uncolored text renders readable on a dark background") {
    LoadResult r = DeckLoader::load(fixture("good_theme.pptx"));
    REQUIRE(r.ok);
    QImage img = SlideRenderer::render(r.presentation, 0, W, H);
    // The "Plain" (no-color) run sits ~y 2e6 of 6.858e6 -> ~29% down. Its text
    // must produce light pixels on the dark (#1E2430) themed background.
    bool found_light = false;
    for (int y = 200; y < 320 && !found_light; ++y) {
        for (int x = 80; x < 700 && !found_light; ++x) {
            const QColor c = img.pixelColor(x, y);
            if (c.red() > 180 && c.green() > 180 && c.blue() > 180) {
                found_light = true;
            }
        }
    }
    CHECK(found_light);
}

// BUG-5 — a paragraph with red/green/blue runs shows ALL three colors, not just
// the first run's.
TEST_CASE("BUG-5: multi-run paragraph renders each run's color") {
    LoadResult r = DeckLoader::load(fixture("good_multiruns.pptx"));
    REQUIRE(r.ok);
    QImage img = SlideRenderer::render(r.presentation, 0, W, H);
    bool red = false, green = false, blue = false;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const QColor c = img.pixelColor(x, y);
            if (c.red() > 150 && c.green() < 90 && c.blue() < 90)
                red = true;
            if (c.green() > 150 && c.red() < 90 && c.blue() < 90)
                green = true;
            if (c.blue() > 150 && c.red() < 90 && c.green() < 90)
                blue = true;
        }
    }
    CHECK(red);
    CHECK(green);
    CHECK(blue);
}

// BUG-10 — a wide image in a square frame is drawn aspect-preserved (centered,
// not squished): the frame's top/bottom bands show background, not stretched image.
TEST_CASE("BUG-10: image aspect is preserved (no squish)") {
    LoadResult r = DeckLoader::load(fixture("good_wideimage.pptx"));
    REQUIRE(r.ok);
    QImage img = SlideRenderer::render(r.presentation, 0, W, H);
    // Frame: off(2e6,2e6) ext(4e6,4e6) on 12.192e6 x 6.858e6 -> square frame at
    // roughly x[210..630], y[210..630] in 1280x720. The 4:1 image fits the width
    // and is centered vertically, so near the frame's top edge is background (black),
    // while the vertical center has red.
    const QColor topOfFrame = img.pixelColor(400, 225); // just inside the frame top
    CHECK(topOfFrame.red() < 60);                       // background, not stretched red
    bool center_red = false;
    for (int y = 380; y < 470 && !center_red; ++y) {
        for (int x = 230; x < 620 && !center_red; ++x) {
            const QColor c = img.pixelColor(x, y);
            if (c.red() > 150 && c.green() < 90 && c.blue() < 90) {
                center_red = true;
            }
        }
    }
    CHECK(center_red);
}

// BUG-4 — long text WRAPS onto multiple lines instead of being truncated to one.
// The text box is tall and narrow, so wrapped content reaches well below the
// first line's height.
TEST_CASE("BUG-4: long text wraps onto multiple lines") {
    LoadResult r = DeckLoader::load(fixture("good_longtext.pptx"));
    REQUIRE(r.ok);
    QImage img = SlideRenderer::render(r.presentation, 0, W, H);
    // Box: off y=500000 of 6.858e6 -> ~52px top; a single line would be < ~80px
    // tall. Wrapped text must paint light pixels well below that (y > 160).
    const QRgb black = qRgb(0, 0, 0);
    int topBand = countNonBackground(img, 0, 40, W, 130, black);
    int lowerBand = countNonBackground(img, 0, 160, W, 400, black);
    CHECK(topBand > 0);
    CHECK(lowerBand > 0); // wrapped continuation exists below the first line
}

// ---------------------------------------------------------------------------
// BUG-37 — the crop must actually change the pixels drawn.
// ---------------------------------------------------------------------------
TEST_CASE("BUG-37: a cropped picture draws only the cropped region") {
    LoadResult r = DeckLoader::load(fixture("good_srcrect.pptx"));
    REQUIRE(r.ok);
    const QImage img = SlideRenderer::render(r.presentation, 0, 800, 400);
    REQUIRE_FALSE(img.isNull());

    auto share = [&img](int x0, int x1, int y0, int y1, auto pred) {
        int hits = 0, total = 0;
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                if (pred(img.pixelColor(x, y))) {
                    ++hits;
                }
                ++total;
            }
        }
        return total > 0 ? double(hits) / total : 0.0;
    };
    auto isRed = [](const QColor& c) { return c.red() > 200 && c.green() < 60 && c.blue() < 60; };

    // Top-left and top-right hold the SAME 4x1 source (white, white, RED, white) at
    // the same size, differing ONLY by the crop. Cropping 25% off each side leaves
    // 2 px of which one is red, so red must cover about twice the share it does in
    // the uncropped copy. If the crop were ignored the two would be identical — the
    // first version of this test could not tell them apart and passed with the fix
    // disabled.
    const int midX = img.width() / 2;
    const int topY0 = 5, topY1 = img.height() / 3;
    const double croppedRed = share(0, midX, topY0, topY1, isRed);
    const double wholeRed = share(midX, img.width(), topY0, topY1, isRed);
    REQUIRE(wholeRed > 0.0); // the uncropped copy really does draw red
    CHECK(croppedRed > wholeRed * 1.5);

    SUBCASE("alphaModFix makes the picture translucent, not opaque") {
        // The third picture is the uncropped source at 50% opacity, so its red
        // composites toward pink over the light slide and must NOT read as pure red.
        const double lowerRed = share(0, midX, img.height() * 2 / 3, img.height() - 5, isRed);
        CHECK(lowerRed < 0.01);
        const double pink =
            share(0, midX, img.height() * 2 / 3, img.height() - 5, [](const QColor& c) {
                return c.red() > 200 && c.green() > 90 && c.green() < 190 && c.blue() > 90 &&
                       c.blue() < 190;
            });
        CHECK(pink > 0.0);
    }
}

// Adversarial-review finding F2 — an over-crop must draw NOTHING, not a mirror.
TEST_CASE("BUG-37 review: an over-crop is rejected, never drawn mirrored") {
    LoadResult r = DeckLoader::load(fixture("good_srcrect_edge.pptx"));
    REQUIRE(r.ok);
    // l+r = 120000 > 100000, so the requested span is empty. Computing it anyway
    // gives a negative width, and QRectF::intersected NORMALISES that — swapping the
    // edges and returning a valid rectangle covering exactly the region the deck
    // excluded, drawn mirrored. The downstream width guard never fires because the
    // normalised width is positive.
    const QImage img = SlideRenderer::render(r.presentation, 0, 1000, 500);
    REQUIRE_FALSE(img.isNull());
    // Map EMU -> pixels through the SAME letterbox the renderer applies, then sample
    // only the middle of that picture's frame. A naive width-proportional window
    // spills into the neighbouring picture, which legitimately draws red — the first
    // version of this test failed for exactly that reason.
    const double slideW = static_cast<double>(r.presentation.slideWidth);
    const double slideH = static_cast<double>(r.presentation.slideHeight);
    const double scale = std::min(img.width() / slideW, img.height() / slideH);
    const double offX = (img.width() - slideW * scale) / 2.0;
    const double frameL = offX + 6000000.0 * scale;
    const double frameR = offX + 8000000.0 * scale;
    const double inset = (frameR - frameL) * 0.2;
    const int x0 = static_cast<int>(frameL + inset);
    const int x1 = static_cast<int>(frameR - inset);
    REQUIRE(x1 > x0);
    int red = 0;
    for (int y = 0; y < img.height(); ++y) {
        for (int x = x0; x < x1; ++x) {
            const QColor c = img.pixelColor(x, y);
            if (c.red() > 200 && c.green() < 60 && c.blue() < 60) {
                ++red;
            }
        }
    }
    CHECK(red == 0);
}

// Adversarial review round 2 — defences that mutation testing proved UNTESTED.
// Both mutations below previously left the entire suite green.
TEST_CASE("BUG-37 review2: a sub-pixel crop draws, and opacity does not leak") {
    LoadResult r = DeckLoader::load(fixture("good_pic_edges.pptx"));
    REQUIRE(r.ok);
    const QImage img = SlideRenderer::render(r.presentation, 0, 1200, 600);
    REQUIRE_FALSE(img.isNull());

    const double slideW = static_cast<double>(r.presentation.slideWidth);
    const double slideH = static_cast<double>(r.presentation.slideHeight);
    const double scale = std::min(img.width() / slideW, img.height() / slideH);
    const double offX = (img.width() - slideW * scale) / 2.0;
    const double offY = (img.height() - slideH * scale) / 2.0;
    auto sample = [&](double emuX, double emuY) {
        return img.pixelColor(static_cast<int>(offX + emuX * scale),
                              static_cast<int>(offY + emuY * scale));
    };

    SUBCASE("opacity is restored, so a translucent picture cannot wash out the next") {
        // The translucent picture is FIRST here and the opaque one SECOND. The
        // original fixture put the translucent one LAST, where a leaked opacity had
        // nothing left to damage — which is why deleting setOpacity(1.0) left the
        // whole suite green while visibly washing out real decks.
        const QColor opaque = sample(4500000, 1500000);
        CHECK(opaque.red() > 200);
        CHECK(opaque.green() < 60);
        CHECK(opaque.blue() < 60);
    }

    SUBCASE("a crop leaving under one SOURCE pixel still draws, never vanishes") {
        // A 60x2 bar cropped b=60000 leaves 0.8 source pixels of height. The guard
        // was `< 1` measured in SOURCE pixels, so it was dropped silently — no
        // warning, no placeholder. That is BUG-41's defect class, reintroduced one
        // commit later, and the real deck uses b=36909.
        // The cropped bar is 60:1, so aspect-preserving fit makes it a HAIRLINE at
        // the vertical centre of its frame (y = 1500000 EMU), not a band. Scan a
        // short vertical span there rather than guessing one pixel — the first
        // version of this assertion sampled empty background and failed.
        bool sawBar = false;
        for (double dy = -60000; dy <= 60000 && !sawBar; dy += 4000) {
            const QColor c = sample(8500000, 1500000 + dy);
            if (c.red() > 150 && c.blue() < 100) {
                sawBar = true;
            }
        }
        CHECK(sawBar);
    }
}

// BUG-53 — <a:stretch> means FILL the frame; without it, preserve aspect.
//
// Every one of the 51 blipFill elements in the reference deck carries <a:stretch>,
// and we were letterboxing all of them — so the crop could be exactly right and the
// picture still the wrong size and shape.
TEST_CASE("BUG-53: a stretched picture fills its frame; an unstretched one does not") {
    LoadResult r = DeckLoader::load(fixture("good_stretch.pptx"));
    REQUIRE(r.ok);
    REQUIRE(r.presentation.slides[0].elements.size() == 2);
    CHECK(r.presentation.slides[0].elements[0].image.stretchToFill);
    CHECK_FALSE(r.presentation.slides[0].elements[1].image.stretchToFill);

    const QImage img = SlideRenderer::render(r.presentation, 0, 1200, 600);
    REQUIRE_FALSE(img.isNull());
    const double slideW = static_cast<double>(r.presentation.slideWidth);
    const double slideH = static_cast<double>(r.presentation.slideHeight);
    const double scale = std::min(img.width() / slideW, img.height() / slideH);
    const double offX = (img.width() - slideW * scale) / 2.0;
    const double offY = (img.height() - slideH * scale) / 2.0;
    auto at = [&](double ex, double ey) {
        return img.pixelColor(static_cast<int>(offX + ex * scale),
                              static_cast<int>(offY + ey * scale));
    };
    auto isRed = [](const QColor& c) { return c.red() > 150 && c.green() < 90 && c.blue() < 90; };

    // Both frames are square (4e6 EMU) and hold the same 4:1 image. Sample near the
    // TOP of each — where an aspect-preserved draw leaves background and a stretched
    // draw does not.
    SUBCASE("with <a:stretch> the frame is filled to its top edge") {
        CHECK(isRed(at(3000000, 1300000)));
    }
    SUBCASE("without <a:stretch> the aspect is preserved and the top is background") {
        CHECK_FALSE(isRed(at(9000000, 1300000)));
        CHECK(isRed(at(9000000, 3000000))); // ...but the image is there, centred
    }
}

// BUG-61 — JPEG decode had no fixture at all. Removing JPEG from the allow-list
// left the entire suite green, while the reference deck carries two JPEG media
// parts that would have rendered as grey "missing image" boxes on the projector.
TEST_CASE("BUG-61: a JPEG picture decodes and draws, not a placeholder") {
    LoadResult r = DeckLoader::load(fixture("good_jpeg_image.pptx"));
    REQUIRE(r.ok);
    REQUIRE(r.presentation.slides[0].elements.size() == 1);
    CHECK_FALSE(r.presentation.slides[0].elements[0].image.imageData.isEmpty());

    const QImage img = SlideRenderer::render(r.presentation, 0, 1200, 600);
    REQUIRE_FALSE(img.isNull());
    // The property under test is "the JPEG DECODED", not what colour it is: the
    // fixture is a hand-built minimal JPEG whose entropy data is imprecise, and the
    // regression being guarded is that removing JPEG from the allow-list turns the
    // picture into drawPlaceholderBox's flat 0xC8C8C8 panel. So assert that no
    // placeholder was drawn and that something WAS.
    int placeholderGrey = 0, drawn = 0;
    for (int y = img.height() / 4; y < img.height() * 3 / 4; ++y) {
        for (int x = img.width() / 6; x < img.width() / 2; ++x) {
            const QColor c = img.pixelColor(x, y);
            if (c.red() == 0xC8 && c.green() == 0xC8 && c.blue() == 0xC8) {
                ++placeholderGrey;
            } else if (c.red() != 0xFF || c.green() != 0xFF || c.blue() != 0xFF) {
                ++drawn; // neither placeholder nor bare slide background
            }
        }
    }
    CHECK(placeholderGrey == 0);
    CHECK(drawn > 0);
}

// BUG-62 — the srcRect out-of-bounds clamps had zero coverage. Six of the reference
// deck's twelve srcRect elements carry NEGATIVE insets, which ask for area outside
// the image; without the clamps Qt is handed a source rect it cannot sample.
TEST_CASE("BUG-62: negative insets and edge crops stay inside the image") {
    LoadResult r = DeckLoader::load(fixture("good_srcrect_negative.pptx"));
    REQUIRE(r.ok);
    const auto& els = r.presentation.slides[0].elements;
    REQUIRE(els.size() == 3);

    SUBCASE("negative insets are parsed as declared, verbatim from the real deck") {
        CHECK(els[0].image.srcRect.rightPerMille == -6769);
        CHECK(els[0].image.srcRect.bottomPerMille == 41679);
        CHECK(els[1].image.srcRect.leftPerMille == -6726);
        CHECK(els[1].image.srcRect.rightPerMille == -1);
    }

    SUBCASE("every picture still renders — an outset must not delete the image") {
        const QImage img = SlideRenderer::render(r.presentation, 0, 1200, 600);
        REQUIRE_FALSE(img.isNull());
        const double slideW = static_cast<double>(r.presentation.slideWidth);
        const double slideH = static_cast<double>(r.presentation.slideHeight);
        const double scale = std::min(img.width() / slideW, img.height() / slideH);
        const double offX = (img.width() - slideW * scale) / 2.0;
        const double offY = (img.height() - slideH * scale) / 2.0;
        for (double cx : {2000000.0, 6000000.0, 10000000.0}) {
            bool red = false;
            for (double cy = 400000.0; cy < 2600000.0 && !red; cy += 100000.0) {
                const QColor c = img.pixelColor(static_cast<int>(offX + cx * scale),
                                                static_cast<int>(offY + cy * scale));
                if (c.red() > 150 && c.blue() < 100) {
                    red = true;
                }
            }
            CHECK(red);
        }
    }
}

// BUG-62 — the clamp itself, asserted directly. A colour-based assertion was too
// coarse: a UAT-4 mutation removing `.intersected(whole)` survived it.
TEST_CASE("BUG-62: the source rect never leaves the image, whatever the deck asks") {
    const QSize img(16, 4);
    SUBCASE("negative insets from the real deck stay inside") {
        // r=-6769 asks for area 6.769% beyond the right edge.
        const QRectF a = slideSourceRect(img, SrcRect{0, 0, -6769, 41679});
        CHECK(a.left() >= 0.0);
        CHECK(a.top() >= 0.0);
        CHECK(a.right() <= 16.0);
        CHECK(a.bottom() <= 4.0);

        const QRectF b = slideSourceRect(img, SrcRect{-6726, 0, -1, 36909});
        CHECK(b.left() >= 0.0);
        CHECK(b.right() <= 16.0);
        CHECK(b.bottom() <= 4.0);
    }
    SUBCASE("an extreme outset is clamped to the whole image, not inverted") {
        const QRectF r = slideSourceRect(img, SrcRect{-500000, -500000, -500000, -500000});
        CHECK(r.left() >= 0.0);
        CHECK(r.top() >= 0.0);
        CHECK(r.right() <= 16.0);
        CHECK(r.bottom() <= 4.0);
        CHECK(r.width() > 0.0);
    }
    SUBCASE("an ordinary crop is untouched by the clamp") {
        const QRectF r = slideSourceRect(img, SrcRect{25000, 0, 25000, 0});
        CHECK(r.left() == doctest::Approx(4.0));
        CHECK(r.width() == doctest::Approx(8.0));
    }
}
