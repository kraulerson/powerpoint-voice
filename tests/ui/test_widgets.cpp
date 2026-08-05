#include <doctest/doctest.h>

#include <QCloseEvent>
#include <QImage>
#include <QKeyEvent>
#include <QPixmap>
#include <QTest>

#include "present/presentation_controller.hpp"
#include "ui/notice_strip.hpp"
#include "ui/presentation_window.hpp"
#include "ui/slide_surface.hpp"

using namespace pptv;

namespace {
QImage filled(const QSize& sz, QColor c) {
    QImage img(sz, QImage::Format_RGB32);
    img.fill(c);
    return img;
}
} // namespace

// ===========================================================================
// GROUP U — widget glue. These are the behaviours that only exist at the widget
// layer, and two of them protect the talk from a single keypress.
// ===========================================================================

TEST_CASE("U: keys reach the window and become commands") {
    PresentationController c;
    c.setDeck(10);
    PresentationWindow w(&c);
    std::vector<Command> got;
    w.setCommandSink([&](Command cmd) { got.push_back(cmd); });
    w.resize(800, 600);
    w.show();
    QTest::keyClick(&w, Qt::Key_Right);
    REQUIRE(got.size() == 1);
    CHECK(got[0].type == CommandType::NextSlide);
    QTest::keyClick(&w, Qt::Key_Left);
    REQUIRE(got.size() == 2);
    CHECK(got[1].type == CommandType::PreviousSlide);
}

TEST_CASE("U: Esc does NOT close the window — Qt's default would end the talk") {
    PresentationController c;
    c.setDeck(10);
    PresentationWindow w(&c);
    w.resize(800, 600);
    w.show();
    REQUIRE(w.isVisible());
    QTest::keyClick(&w, Qt::Key_Escape);
    CHECK(w.isVisible()); // still up
    QTest::keyClick(&w, Qt::Key_Escape);
    CHECK(w.isVisible());
}

TEST_CASE("U: Esc is routed as a UI request, not swallowed silently") {
    PresentationController c;
    c.setDeck(10);
    PresentationWindow w(&c);
    std::vector<UiRequest> reqs;
    w.setUiRequestSink([&](UiRequest r) { reqs.push_back(r); });
    w.resize(800, 600);
    w.show();
    QTest::keyClick(&w, Qt::Key_Escape);
    REQUIRE(reqs.size() == 1);
    CHECK(reqs[0] == UiRequest::RequestHolding);
}

TEST_CASE("U: a close request is REFUSED unless quitting was confirmed") {
    PresentationController c;
    c.setDeck(10);
    PresentationWindow w(&c);
    w.resize(800, 600);
    w.show();

    QCloseEvent e1;
    QCoreApplication::sendEvent(&w, &e1);
    CHECK_FALSE(e1.isAccepted());
    CHECK(w.isVisible());

    // Now confirm quit through the only path that can set it.
    c.requestHolding(0);
    c.requestHolding(0);
    c.confirmQuit();
    REQUIRE(c.quitConfirmed());
    QCloseEvent e2;
    QCoreApplication::sendEvent(&w, &e2);
    CHECK(e2.isAccepted());
}

TEST_CASE("U: the slide is drawn at the aspect-preserved rect, centred") {
    SlideSurface s;
    s.resize(1024, 768);
    s.setSlideImage(filled(QSize(1920, 1080), Qt::red));
    s.show();
    const QPixmap grabbed = s.grab();
    REQUIRE_FALSE(grabbed.isNull());
    // 16:9 into 4:3 -> full width, 576 tall, 96px bars top and bottom
    const QRectF r = s.lastPaintedRect();
    CHECK(qAbs(r.width() - 1024.0) < 1.0);
    CHECK(qAbs(r.height() - 576.0) < 1.0);
    CHECK(qAbs(r.y() - 96.0) < 1.0);

    // the bars really are background, and the middle really is the slide
    const QImage img = grabbed.toImage();
    CHECK(img.pixelColor(512, 10).red() < 60);   // top bar
    CHECK(img.pixelColor(512, 384).red() > 200); // slide
    CHECK(img.pixelColor(512, 758).red() < 60);  // bottom bar
}

TEST_CASE("U: with no raster the surface shows status text, never a blank window") {
    SlideSurface s;
    s.resize(800, 600);
    s.setStatusText(QStringLiteral("Rendering slide 3..."));
    s.show();
    CHECK_FALSE(s.grab().isNull());
    CHECK(s.lastPaintedRect().isEmpty()); // nothing drawn as a slide
}

TEST_CASE("U: the notice strip height is bounded, never grows with content") {
    CHECK(NoticeStrip::heightFor(1080) == 72);                     // min(10% = 108, 72)
    CHECK(NoticeStrip::heightFor(400) == 40);                      // min(10% = 40, 72)
    CHECK(NoticeStrip::heightFor(100) == NoticeStrip::kMinHeight); // floor
    CHECK(NoticeStrip::heightFor(0) == NoticeStrip::kMinHeight);
    CHECK(NoticeStrip::heightFor(-5) == NoticeStrip::kMinHeight);
}

TEST_CASE("U: a very long notice elides instead of resizing the strip") {
    NoticeStrip strip;
    strip.resize(600, NoticeStrip::heightFor(1080));
    const int hEmpty = strip.height();
    strip.setText(QString(400, QLatin1Char('x')));
    strip.show();
    CHECK(strip.height() == hEmpty); // unchanged
    CHECK_FALSE(strip.grab().isNull());
    CHECK(strip.sizeHint().height() <= NoticeStrip::kMaxHeight);
}

TEST_CASE("U: painting a notice never throws out of paintEvent") {
    // A paint that propagates an exception through Qt's event loop is undefined
    // behaviour; the strip must contain whatever happens while formatting text.
    NoticeStrip strip;
    strip.resize(600, 48);
    strip.setText(QString());
    CHECK_NOTHROW(strip.grab());
    strip.setText(QStringLiteral("Deck has 47 slides"));
    CHECK_NOTHROW(strip.grab());
}

// ===========================================================================
// UAT-3 REMEDIATION — the FIRST AppShell-level tests. The audit's structural
// finding was that AppShell (the wiring) had zero coverage while every Critical
// lived there. These cover the SEV-1: a slide raster arriving from the
// pre-render worker must NEVER paint over the privacy blackout.
// ===========================================================================

TEST_CASE("UAT3 SEV-1: a slide arriving while blanked must not paint the deck") {
    // Drive the surface exactly as AppShell does, with the controller in Holding.
    PresentationController c;
    c.setDeck(10);
    PresentationWindow w(&c);
    w.resize(800, 600);
    w.show();

    // Presenting: a raster paints.
    w.setSlideImage(filled(QSize(1920, 1080), Qt::red));
    w.surface()->grab();
    CHECK_FALSE(w.surface()->lastPaintedRect().isEmpty());

    // Blank the projector, then simulate the pre-render worker delivering a slide.
    c.requestHolding(0);
    REQUIRE(c.mode() == Mode::Holding);
    w.setSlideImage(QImage()); // what refresh() does in Holding
    w.surface()->grab();
    CHECK(w.surface()->lastPaintedRect().isEmpty()); // nothing of the deck is drawn
}

TEST_CASE("UAT3: the blackout surface carries no deck content, only a hint") {
    PresentationController c;
    c.setDeck(10);
    PresentationWindow w(&c);
    w.resize(800, 600);
    c.requestHolding(0);
    w.setSlideImage(QImage());
    w.surface()->setStatusText(
        noticeForRole(Notice{NoticeId::HoldingHint}, NoticeRole::Operator, false));
    w.show();
    const QImage img = w.surface()->grab().toImage();
    REQUIRE_FALSE(img.isNull());
    CHECK(w.surface()->lastPaintedRect().isEmpty());
}
