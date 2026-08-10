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

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>

#include <QAccessible>
#include <QAccessibleEvent>

#include "audio/audio_capture.hpp"
#include "command/vosk_engine.hpp"
#include "present/presentation_controller.hpp"
#include "ui/a11y_announce.hpp"
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

// BUG-42 — quitting could take 5 s with every further quit request discarded.
//
// After exec() returns the GUI thread has NO RUNLOOP while teardown blocks, so macOS
// never re-invokes applicationShouldTerminate: and additional Dock / Activity Monitor
// quits vanish. From outside that is indistinguishable from refusing to die.
//
// The tempting fix — skip the wait, detach the thread — was MEASURED WRONG by an
// adversarial reviewer: 8 of 14 runs SEGV, because ~QGuiApplication tears down the
// font database while the render thread is still inside QFont/QPainter.
TEST_CASE("Q/BUG-42: an application quit is recognised as terminating, and stays so") {
    // The flag must survive past the closeAllWindows() scope, because teardown runs
    // long after it — that is the whole point of it being separate from
    // applicationQuitInProgress().
    CHECK_FALSE(applicationQuitInProgress()); // scoped flag is down between quits

    PresentationController c;
    c.setDeck(3);
    PresentationWindow w(&c);
    w.show();
    QEvent quit(QEvent::Quit);
    QCoreApplication::sendEvent(qApp, &quit);

    CHECK(applicationIsTerminating());        // sticky: still true after the scope ended
    CHECK_FALSE(applicationQuitInProgress()); // scoped: already back down
    CHECK_FALSE(w.isVisible());
}

// ===========================================================================
// F-2 (Phase 3) — the P key was DEAD, and the whole suite stayed green.
//
// PresentationWindow held a `paused_` flag with ZERO writers, so KeyContext::paused
// was permanently false, P always translated to PausePresentation, and
// PresentationController treats pause and continue as no-ops. Pressing P did
// nothing at all — while looking, to the presenter, exactly like the pause they
// were relying on before taking questions from a room of twenty people.
//
// It survived because the wiring that gives the key meaning lives in AppShell, and
// AppShell could not be driven without a microphone and a real deck. The two seams
// used here install exactly that wiring and nothing else.
// ===========================================================================

TEST_CASE("W/F-2: the P key actually pauses the voice gate, and toggles back") {
    // The shell is heap-allocated so it can be destroyed BEFORE the window it holds,
    // while the window is still built on the shell's OWN controller. The first
    // version of this test used a separate local controller, which left one
    // assertion unfalsifiable — the window read one object and the sink wrote another
    // (BUG-85).
    auto* shell = new AppShell();
    shell->controllerForTest().setDeck(10);
    PresentationWindow w(&shell->controllerForTest());
    shell->installVoiceGateForTest();
    shell->installWindowSinksForTest(&w);
    w.resize(800, 600);
    w.show();

    REQUIRE_FALSE(shell->voiceGatePausedForTest());

    QTest::keyClick(&w, Qt::Key_P);
    CHECK(shell->voiceGatePausedForTest()); // P PAUSES — it used to do nothing

    QTest::keyClick(&w, Qt::Key_P);
    CHECK_FALSE(shell->voiceGatePausedForTest()); // ...and P resumes

    QTest::keyClick(&w, Qt::Key_P);
    CHECK(shell->voiceGatePausedForTest()); // a real toggle, not a one-shot

    // P must never move the deck — now assertable, because this is the controller
    // the sink dispatches into.
    CHECK(shell->controllerForTest().currentSlide1Based() == 1);
    delete shell;
}

TEST_CASE("W/F-2: pausing gates VOICE, never the keyboard") {
    // The keyboard is the guaranteed control path and nothing may take it away
    // mid-talk (F8b/F8c audits). Asserted at the window, with the shell's wiring out
    // of the way, so it stays true regardless of what the gate is doing.
    PresentationController c;
    c.setDeck(10);
    PresentationWindow w(&c);
    std::vector<Command> got;
    w.setCommandSink([&](Command cmd) { got.push_back(cmd); });
    w.resize(800, 600);
    w.show();

    w.setPaused(true);
    QTest::keyClick(&w, Qt::Key_Right);
    REQUIRE(got.size() == 1);
    CHECK(got[0].type == CommandType::NextSlide); // still navigates while paused

    // ...and while paused, P asks to CONTINUE rather than pausing a second time.
    QTest::keyClick(&w, Qt::Key_P);
    REQUIRE(got.size() == 2);
    CHECK(got[1].type == CommandType::ContinuePresentation);

    // The flag is what decides that, which is precisely why it needed a writer.
    w.setPaused(false);
    QTest::keyClick(&w, Qt::Key_P);
    REQUIRE(got.size() == 3);
    CHECK(got[2].type == CommandType::PausePresentation);
}

TEST_CASE("W/F-2: with no voice armed, P is inert rather than falsely reassuring") {
    // Nothing to gate, so claiming "Resumed" would be a lie the presenter acts on.
    auto* shell = new AppShell(); // deliberately NO installVoiceGateForTest()
    shell->controllerForTest().setDeck(10);
    PresentationWindow w(&shell->controllerForTest());
    shell->installWindowSinksForTest(&w);
    w.resize(800, 600);
    w.show();

    QTest::keyClick(&w, Qt::Key_P);
    CHECK_FALSE(shell->voiceGatePausedForTest());
    // Assertable now, on the controller the sink actually writes to (BUG-85). A
    // right-arrow moves it, which is what proves the check has teeth.
    CHECK(shell->controllerForTest().currentSlide1Based() == 1);
    QTest::keyClick(&w, Qt::Key_Right);
    CHECK(shell->controllerForTest().currentSlide1Based() == 2);
    delete shell;
}

// ===========================================================================
// F-CHAOS-2 (Phase 3) — an abandoned render worker painted the PREVIOUS deck.
//
// teardownWorkers() deliberately ABANDONS a worker whose wait expires rather than
// terminating it (terminate() can strand allocator locks; destroying a running
// QThread is a qFatal abort). An abandoned render worker keeps rasterising the old
// deck — and its slideReady was still connected here, so those slides landed in
// rasters_ AFTER openDeck() had resized it for the new deck. Every index is in
// range, so nothing complained: the projector simply showed the old deck's content
// under the new deck's slide numbers.
// ===========================================================================

TEST_CASE("W/F-CHAOS-2: a raster from a previous deck is dropped, not painted") {
    AppShell shell;
    shell.openDeckGenerationForTest(5);
    const int stale = shell.deckGenerationForTest();

    // The first deck's worker delivers a slide — accepted, it is the current deck.
    shell.deliverSlideForTest(stale, 0, filled(QSize(64, 36), Qt::red));
    REQUIRE(shell.hasRasterForTest(0));

    // The presenter opens a different deck. The old worker was not stopped; it is
    // still rendering, and it still has slides to hand over.
    shell.openDeckGenerationForTest(5);
    const int current = shell.deckGenerationForTest();
    REQUIRE(current != stale);
    REQUIRE_FALSE(shell.hasRasterForTest(0)); // fresh, empty raster set

    shell.deliverSlideForTest(stale, 0, filled(QSize(64, 36), Qt::red));
    shell.deliverSlideForTest(stale, 3, filled(QSize(64, 36), Qt::red));
    CHECK_FALSE(shell.hasRasterForTest(0)); // the OLD deck's pixels never land
    CHECK_FALSE(shell.hasRasterForTest(3));

    // ...while the new deck's own worker is unaffected.
    shell.deliverSlideForTest(current, 3, filled(QSize(64, 36), Qt::blue));
    CHECK(shell.hasRasterForTest(3));
}

// ===========================================================================
// A11Y-1 (Phase 3) — the presentation was silent to VoiceOver.
//
// Every surface here is a custom-painted QWidget. To a screen reader that is an
// unnamed rectangle with no text in it, so the deck, the privacy blackout, the quit
// prompt and a crashed app were all indistinguishable: silence. The window has no
// menu bar and no on-screen controls either, so there was nothing to discover the
// key map from.
//
// The constraint that shapes all of this: the accessibility tree is readable by
// other processes, and the deck is Confidential. So the state is named — "Slide 3
// of 10" — and its CONTENT never is (Bible section 8, TM-012/013).
// ===========================================================================

TEST_CASE("W/A11Y-1: the presentation window names itself and its key map") {
    PresentationController c;
    c.setDeck(10);
    PresentationWindow w(&c);
    CHECK_FALSE(w.accessibleName().isEmpty());
    // The key map is the only way in: no menu, no toolbar, no visible control.
    const QString desc = w.accessibleDescription();
    CHECK(desc.contains(QStringLiteral("arrow")));
    CHECK(desc.contains(QStringLiteral("Escape")));
    CHECK(desc.contains(QStringLiteral("P ")));
}

TEST_CASE("W/A11Y-1: the slide surface says WHICH slide, and never what is on it") {
    SlideSurface s;
    CHECK_FALSE(s.accessibleName().isEmpty());

    s.setAccessibleState(QStringLiteral("Slide 3 of 10."));
    CHECK(s.accessibleDescription() == QStringLiteral("Slide 3 of 10."));

    // A blanked projector must be distinguishable from a broken app.
    s.setAccessibleState(QStringLiteral("Projector blanked. The deck is hidden."));
    CHECK(s.accessibleDescription().contains(QStringLiteral("blanked")));
}

TEST_CASE("W/A11Y-1: a notice is announced, and repeats are not") {
    NoticeStrip strip;
    CHECK_FALSE(strip.accessibleName().isEmpty());

    strip.setText(QStringLiteral("Deck has 10 slides"));
    CHECK(strip.accessibleDescription() == QStringLiteral("Deck has 10 slides"));

    // Clearing is reflected too, so a stale notice is not left in the tree for a
    // screen reader to read back long after it faded from the screen.
    strip.setText(QString());
    CHECK(strip.accessibleDescription().isEmpty());
}

TEST_CASE("W/A11Y-1: the shell actually WIRES the spoken state to the presentation") {
    // The naming above is worth nothing if nobody updates it — that is BUG-60's
    // lesson, and this is the equivalent assertion for the accessibility tree.
    PresentationController c;
    PresentationWindow w(&c); // declared first: it must outlive the shell
    AppShell shell;
    shell.installWindowSinksForTest(&w);
    shell.openDeckGenerationForTest(10);

    shell.deliverSlideForTest(shell.deckGenerationForTest(), 0, filled(QSize(64, 36), Qt::red));
    CHECK(w.surface()->accessibleDescription() == QStringLiteral("Slide 1 of 10."));
    // ...and it does NOT leak what is on the slide.
    CHECK_FALSE(w.surface()->accessibleDescription().contains(QStringLiteral("red")));
}

// ===========================================================================
// BUG-72 / F-1, PROPERLY PINNED THIS TIME (BUG-79).
//
// The first attempt at this test asserted a property of VoicePipeline against a
// fake capture that joins BY CONSTRUCTION — so it asserted a property of the fake.
// An adversarial reviewer deleted the entire F-1 fix (the teardownVoice() call AND
// the member declaration order) and all 279 tests stayed green.
//
// What BUG-72 is actually about is WHEN the VoskEngine dies relative to the audio
// thread, and nothing outside AppShell could see that. VoskEngine now counts its own
// destructions, which makes it visible without dereferencing anything that might
// already be freed.
// ===========================================================================

namespace {
// Behaves like a real capture device: drives the sink from its own thread, and its
// stop() joins that thread. MiniaudioCapture's equivalent barrier is sinkMutex_.
class ShellThreadedCapture : public IAudioCapture {
  public:
    ~ShellThreadedCapture() override { stop(); }
    CaptureError start() override {
        running_ = true;
        thread_ = std::thread([this] {
            const std::vector<std::int16_t> buf(960 * 2, 0);
            while (!quit_.load()) {
                if (sink_) {
                    sink_(buf.data(), buf.size());
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
        return CaptureError::None;
    }
    void stop() override {
        quit_ = true;
        if (thread_.joinable()) {
            thread_.join();
        }
        running_ = false;
    }
    bool isRunning() const override { return running_.load(); }
    AudioFormat deviceFormat() const override { return AudioFormat{48000, 2}; }
    void setSink(CaptureSink s) override { sink_ = std::move(s); }

  private:
    CaptureSink sink_;
    std::atomic<bool> running_{false};
    std::atomic<bool> quit_{false};
    std::thread thread_;
};
} // namespace

TEST_CASE("W/F-1: the shell never frees the speech engine under a live decode") {
    std::atomic<int> entries{0};
    std::atomic<long> atEntry{-1};
    std::atomic<long> atExit{-1};

    auto* shell = new AppShell();
    shell->installVoiceForTest(std::make_unique<ShellThreadedCapture>(), [&] {
        ++entries;
        atEntry = VoskEngine::destructionCountForTest();
        // Stands in for VoskEngine::feed(), which is not instantaneous. This is the
        // window in which the engine must not be freed.
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        atExit = VoskEngine::destructionCountForTest();
    });

    // Make sure a decode is genuinely in flight, so the destructor is racing
    // something real rather than an idle thread.
    for (int i = 0; i < 200 && entries.load() == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(entries.load() > 0);

    delete shell;

    // THE assertion. If any VoskEngine was destroyed between a decode starting and
    // that same decode finishing, the shell freed the engine underneath the audio
    // thread — which is BUG-72 exactly, and is what a Cmd+Q at the end of a talk
    // does on a machine that has a microphone.
    REQUIRE(atEntry.load() >= 0);
    REQUIRE(atExit.load() >= 0);
    CHECK(atEntry.load() == atExit.load());
}

// ===========================================================================
// BUG-80 — the announcements were a NO-OP on the only platform this ships on.
//
// The first A11Y-1 fix raised QAccessible::Alert and QAccessible::DescriptionChanged.
// Neither has an AppKit equivalent, so Qt's Cocoa plugin discards both and VoiceOver
// says nothing. `nm -mu libqcocoa.dylib` lists the complete set it can post:
// Focused / SelectedText / Title / ValueChanged notifications, plus
// NSAccessibilityAnnouncementRequestedNotification with AnnouncementKey and
// PriorityKey. Only the last carries a message.
//
// The four earlier A11Y tests asserted the accessible DESCRIPTION strings, which the
// no-op version set perfectly well — so they passed while nothing was ever spoken.
// These assert the event that actually reaches the platform.
// ===========================================================================

namespace {
// QAccessible's update handler is a bare function pointer, so the capture has to be
// file-scope. Reset before each use.
std::vector<QAccessible::Event> g_a11yEvents;
std::vector<QString> g_a11yMessages;

void captureA11y(QAccessibleEvent* ev) {
    if (ev == nullptr) {
        return;
    }
    g_a11yEvents.push_back(ev->type());
    if (ev->type() == QAccessible::Announcement) {
        g_a11yMessages.push_back(static_cast<QAccessibleAnnouncementEvent*>(ev)->message());
    }
}

// Turns the accessibility bridge on for the duration of a test and restores whatever
// was there before, so one test cannot leak a handler into the next.
struct A11yCapture {
    bool wasActive;
    QAccessible::UpdateHandler previous;
    A11yCapture() : wasActive(QAccessible::isActive()) {
        g_a11yEvents.clear();
        g_a11yMessages.clear();
        QAccessible::setActive(true);
        previous = QAccessible::installUpdateHandler(&captureA11y);
    }
    ~A11yCapture() {
        QAccessible::installUpdateHandler(previous);
        QAccessible::setActive(wasActive);
    }
};
} // namespace

TEST_CASE("W/BUG-80: a notice reaches the platform as an ANNOUNCEMENT, with its text") {
    // Qt 6.8 introduced the only event type macOS can act on. This project SHIPS on
    // macOS/Qt 6.11, so that path is the one that matters; CI's Ubuntu Qt is older
    // and compiles a documented no-op (BUG-87). Asserted against the build's own
    // capability rather than assumed either way.
    if (!accessibilityAnnouncementsAvailable()) {
        MESSAGE("Qt < 6.8: no announcement API on this build — nothing to assert.");
        return;
    }
    A11yCapture capture;
    NoticeStrip strip;
    strip.setText(QStringLiteral("Deck has 10 slides"));

    REQUIRE_FALSE(g_a11yEvents.empty());
    // Alert and DescriptionChanged are both dropped by the macOS plugin. If either
    // is what we raised, nothing is ever spoken.
    CHECK(std::find(g_a11yEvents.begin(), g_a11yEvents.end(), QAccessible::Announcement) !=
          g_a11yEvents.end());
    CHECK(std::find(g_a11yEvents.begin(), g_a11yEvents.end(), QAccessible::Alert) ==
          g_a11yEvents.end());
    REQUIRE_FALSE(g_a11yMessages.empty());
    CHECK(g_a11yMessages.front() == QStringLiteral("Deck has 10 slides"));
}

TEST_CASE("W/BUG-80: a slide change is announced, and an unchanged state is not") {
    if (!accessibilityAnnouncementsAvailable()) {
        MESSAGE("Qt < 6.8: no announcement API on this build — nothing to assert.");
        return;
    }
    A11yCapture capture;
    SlideSurface s;

    s.setAccessibleState(QStringLiteral("Slide 4 of 10."));
    REQUIRE(g_a11yMessages.size() == 1);
    CHECK(g_a11yMessages.front() == QStringLiteral("Slide 4 of 10."));

    // Repainting the same slide must not make a screen reader say it again.
    s.setAccessibleState(QStringLiteral("Slide 4 of 10."));
    CHECK(g_a11yMessages.size() == 1);

    s.setAccessibleState(QStringLiteral("Projector blanked."));
    CHECK(g_a11yMessages.size() == 2);
}

TEST_CASE("W/F-CHAOS-2: the REAL openDeck path bumps the generation every time") {
    // The other F-CHAOS-2 test drives acceptSlide() directly, which a reviewer
    // rightly called a near-tautology of the two-line guard. This one goes through
    // AppShell::openDeck itself, so deleting `++deckGeneration_` — the line every
    // stale-raster drop depends on — is caught.
    //
    // A nonexistent path is deliberate: the observable is the generation, which is
    // bumped before any file is touched, and no deck on disk is needed to see it.
    AppShell shell;
    const int before = shell.deckGenerationForTest();
    shell.openDeck(QStringLiteral("/nonexistent/one.pptx"));
    const int afterFirst = shell.deckGenerationForTest();
    shell.openDeck(QStringLiteral("/nonexistent/two.pptx"));
    const int afterSecond = shell.deckGenerationForTest();

    CHECK(afterFirst == before + 1);
    CHECK(afterSecond == afterFirst + 1);

    // And a raster carrying the FIRST deck's generation is refused by the shell that
    // has moved on — the property the whole guard exists for.
    shell.openDeckGenerationForTest(3); // sizes rasters_ for the current generation
    shell.deliverSlideForTest(afterFirst, 0, filled(QSize(32, 18), Qt::red));
    CHECK_FALSE(shell.hasRasterForTest(0));
}
