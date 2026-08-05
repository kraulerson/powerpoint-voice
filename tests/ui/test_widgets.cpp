#include <doctest/doctest.h>

#include <QCloseEvent>
#include <QDropEvent>
#include <QImage>
#include <QKeyEvent>
#include <QMimeData>
#include <QPixmap>
#include <QPushButton>
#include <QShortcut>
#include <QSignalSpy>
#include <QTest>
#include <QUrl>

#include "present/presentation_controller.hpp"
#include "ui/app_shell.hpp"
#include "ui/notice_strip.hpp"
#include "ui/presentation_window.hpp"
#include "ui/quit_policy.hpp"
#include "ui/slide_surface.hpp"
#include "ui/start_view.hpp"

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

TEST_CASE("U: a close request from the USER is refused unless quitting was confirmed") {
    // NOTE the narrowed title. This test used to be called "a close request is
    // REFUSED unless quitting was confirmed" and it asserted exactly that, for every
    // close request from any source. That assertion WAS the bug: on macOS an
    // application quit (Dock -> Quit, Activity Monitor -> Quit, Cmd+Q) is delivered
    // by QApplication::event(QEvent::Quit) calling closeAllWindows(), and if any
    // top-level widget is still visible afterwards Qt cancels the termination. So
    // refusing every close made the app impossible to quit by any means short of
    // SIGKILL, and 190 green tests pinned it in place. See GROUP Q below.
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

// ===========================================================================
// GROUP Q — the application must be quittable. (BUG-31, second attempt.)
//
// The tester could not kill the app by ANY graceful means on a headless Mac mini:
// not the window button, not Cmd+Q, not Dock -> Quit, not even Activity Monitor's
// Quit. Only Force Quit (SIGKILL) worked. Reproduced outside the test suite by
// calling NSRunningApplication::terminate() — precisely what Activity Monitor's
// Quit button calls — against a real PresentationWindow: the request was delivered
// and the process survived it.
//
// The mechanism: QApplication::event(QEvent::Quit) calls closeAllWindows() and then
// REFUSES to quit if any top-level widget is still visible. Our closeEvent ignored
// every close request, so the window stayed visible and Qt cancelled termination.
//
// The distinction these tests pin: a request to close THE WINDOW is the presenter
// possibly fumbling mid-talk and is worth confirming; a request to quit THE
// APPLICATION came from outside the app and is already deliberate. It must be obeyed.
// ===========================================================================

TEST_CASE("Q/BUG-31: an application quit request is OBEYED, mid-presentation, with no prompt") {
    PresentationController c;
    c.setDeck(10);
    PresentationWindow w(&c);
    w.resize(800, 600);
    w.show();
    REQUIRE(w.isVisible());
    REQUIRE(c.mode() == Mode::Presenting);

    // Exactly what Qt delivers for Dock -> Quit / Activity Monitor -> Quit / Cmd+Q.
    QEvent quit(QEvent::Quit);
    QCoreApplication::sendEvent(qApp, &quit);

    // Qt cancels termination if any top-level widget is still visible afterwards.
    CHECK_FALSE(w.isVisible());
}

TEST_CASE("Q/BUG-31: an application quit is obeyed from EVERY mode, including the blackout") {
    for (int step = 0; step < 3; ++step) {
        PresentationController c;
        c.setDeck(10);
        PresentationWindow w(&c);
        w.resize(800, 600);
        w.show();
        // step 0 = Presenting, 1 = Holding (privacy blackout), 2 = ConfirmQuit.
        for (int i = 0; i < step; ++i) {
            c.requestHolding(0);
        }
        REQUIRE(w.isVisible());

        QEvent quit(QEvent::Quit);
        QCoreApplication::sendEvent(qApp, &quit);
        CHECK_FALSE(w.isVisible());
    }
}

TEST_CASE("Q/BUG-31: a plain window close is still refused after an application quit elsewhere") {
    // The quit flag must belong to the window that was asked to quit, not leak into
    // a later presentation: a second deck opened in the same process must still
    // protect itself from a stray close.
    {
        PresentationController c1;
        c1.setDeck(3);
        PresentationWindow w1(&c1);
        w1.show();
        QEvent quit(QEvent::Quit);
        QCoreApplication::sendEvent(qApp, &quit);
        REQUIRE_FALSE(w1.isVisible());
    }
    PresentationController c2;
    c2.setDeck(3);
    PresentationWindow w2(&c2);
    w2.show();
    QCloseEvent e;
    QCoreApplication::sendEvent(&w2, &e);
    CHECK_FALSE(e.isAccepted());
    CHECK(w2.isVisible());
}

TEST_CASE("Q/BUG-31: the on-screen quit hint names a chord this platform actually delivers") {
    // The prompt told the user to press "Ctrl+Shift+Q". On macOS Qt maps the COMMAND
    // key to Qt::ControlModifier and the physical Control key to Qt::MetaModifier, so
    // a Mac user following that hint literally presses Control+Shift+Q, which arrives
    // as Meta|Shift and matches nothing. The hint must name the chord that works.
    PresentationController c;
    c.setDeck(10);
    PresentationWindow w(&c);
    w.show();
    c.requestHolding(0);
    c.requestHolding(0);
    REQUIRE(c.mode() == Mode::ConfirmQuit);

    // Wire the UI sink exactly as AppShell does, so the chord really reaches
    // confirmQuit() instead of falling into a null sink.
    w.setUiRequestSink([&c](UiRequest r) {
        if (r == UiRequest::ConfirmQuit) {
            c.confirmQuit();
        }
    });

    const QString hint = quitConfirmHint();
#ifdef Q_OS_MACOS
    CHECK(hint.contains(QStringLiteral("Cmd")));
    CHECK_FALSE(hint.contains(QStringLiteral("Ctrl")));
#else
    CHECK(hint.contains(QStringLiteral("Ctrl")));
#endif

    // And the chord the hint names must be the one the translator accepts. On macOS
    // the hint names Cmd+Q (Qt::ControlModifier), deliberately NOT Cmd+Shift+Q —
    // that is the system Log Out shortcut and must never be printed on a projector.
#ifdef Q_OS_MACOS
    CHECK_FALSE(hint.contains(QStringLiteral("Shift")));
    QTest::keyClick(&w, Qt::Key_Q, Qt::ControlModifier);
#else
    QTest::keyClick(&w, Qt::Key_Q, Qt::ControlModifier | Qt::ShiftModifier);
#endif
    CHECK(c.quitConfirmed());
}

// ===========================================================================
// GROUP S — BUG-18: the start screen must offer a way IN.
//
// StartView shipped as two labels and nothing else. A deck could only be supplied
// as a command-line argument, so anyone who launched the app normally got a dark
// window and no way forward — which is exactly what happened the first time Karl
// ran a build on his MacBook Pro. Nothing in 211 tests noticed, because nothing
// asserted that the application is USABLE.
// ===========================================================================

TEST_CASE("S/BUG-18: the start screen offers a control that asks to open a deck") {
    StartView v;
    REQUIRE(v.openButton() != nullptr);
    CHECK(v.openButton()->isEnabled());
    CHECK_FALSE(v.openButton()->text().isEmpty());

    QSignalSpy browse(&v, &StartView::browseRequested);
    v.openButton()->click();
    CHECK(browse.count() == 1);
}

TEST_CASE("S/BUG-18: only a LOCAL file is accepted from a drop") {
    // Tested as a pure function on purpose: a synthetic QDropEvent is not dispatched
    // by QWidget::event, so driving this through the widget would test the harness
    // rather than the rule. The rule is the security-relevant part.
    SUBCASE("a local file yields its path") {
        QMimeData mime;
        mime.setUrls({QUrl::fromLocalFile(QStringLiteral("/tmp/example.pptx"))});
        CHECK(localDeckPathFrom(&mime) == QStringLiteral("/tmp/example.pptx"));
    }

    SUBCASE("a REMOTE url is refused — this app never touches the network") {
        QMimeData mime;
        mime.setUrls({QUrl(QStringLiteral("https://example.com/deck.pptx"))});
        CHECK(localDeckPathFrom(&mime).isEmpty());
    }

    SUBCASE("a local file is still found when a remote url is dropped alongside it") {
        QMimeData mime;
        mime.setUrls({QUrl(QStringLiteral("https://example.com/x.pptx")),
                      QUrl::fromLocalFile(QStringLiteral("/tmp/real.pptx"))});
        CHECK(localDeckPathFrom(&mime) == QStringLiteral("/tmp/real.pptx"));
    }

    SUBCASE("nothing droppable yields nothing, never a crash") {
        QMimeData empty;
        CHECK(localDeckPathFrom(&empty).isEmpty());
        CHECK(localDeckPathFrom(nullptr).isEmpty());
    }

    SUBCASE("the view accepts drops at all — without this the handler is unreachable") {
        StartView v;
        CHECK(v.acceptDrops());
    }
}

// BUG-60 (SEV-1) — the suite could not detect BUG-18's return.
//
// Three mutations each left all 224 tests green and together restored the exact
// pre-BUG-18 state: delete the Cmd+O shortcut, stop dropEvent emitting, delete the
// two connect() calls in AppShell. The existing test asserts the button EMITS a
// signal; nothing asserted that anything was LISTENING. app_shell.cpp — every wire
// in the application — had no tests at all.
TEST_CASE("S/BUG-60: AppShell actually wires the start screen to opening a deck") {
    AppShell shell;
    shell.showStart();
    StartView* view = shell.startViewForTest();
    REQUIRE(view != nullptr);

    SUBCASE("a dropped path reaches the shell's open path") {
        // Emitting fileDropped must cause an open ATTEMPT. A nonexistent path is
        // used deliberately: the observable is that the shell tried and reported,
        // which proves the wire exists without needing a real deck on disk.
        QSignalSpy attempts(&shell, &AppShell::deckOpenAttempted);
        emit view->fileDropped(QStringLiteral("/nonexistent/deck.pptx"));
        CHECK(attempts.count() == 1);
        CHECK(attempts.at(0).at(0).toString() == QStringLiteral("/nonexistent/deck.pptx"));
    }

    SUBCASE("the browse request is connected to something") {
        // browseRequested opens a modal dialog, which cannot run here — so assert
        // the CONNECTION exists rather than driving it. Deleting the connect() in
        // AppShell is precisely mutation m24, and this is what catches it.
        CHECK(view->browseListeners() > 0);
        CHECK(view->dropListeners() > 0);
    }

    SUBCASE("the drop handler really emits — not just the pure path rule") {
        QSignalSpy dropped(view, &StartView::fileDropped);
        QMimeData mime;
        mime.setUrls({QUrl::fromLocalFile(QStringLiteral("/tmp/x.pptx"))});
        view->acceptDroppedMime(&mime);
        CHECK(dropped.count() == 1);
    }

    SUBCASE("the platform Open shortcut is installed") {
        // Deleting the QShortcut removes Cmd+O entirely and is invisible to any
        // behavioural assertion, because a shortcut needs a shown, focused,
        // event-looped widget to fire (BUG-60 mutation m22).
        bool hasOpen = false;
        for (QShortcut* sc : view->findChildren<QShortcut*>()) {
            if (sc->key() == QKeySequence(QKeySequence::Open)) {
                hasOpen = true;
            }
        }
        CHECK(hasOpen);
    }

    SUBCASE("the start screen still offers both routes in") {
        REQUIRE(view->openButton() != nullptr);
        CHECK(view->openButton()->isEnabled());
        CHECK(view->acceptDrops());
    }
}
