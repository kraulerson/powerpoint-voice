#include "ui/app_shell.hpp"

#include <QApplication>
#include <QFileDialog>
#include <QMessageBox>
#include <QScreen>
#include <QThread>
#include <QTimer>

#include "loader/deck_loader.hpp"
#include "present/display_geometry.hpp"
#include "render/slide_renderer.hpp"
#include "ui/presentation_window.hpp"
#include "ui/slide_surface.hpp"
#include "ui/start_view.hpp"

namespace pptv {

namespace {

// The screens we might present on, as plain data, so the pure policy decides.
std::vector<ScreenInfo> currentScreens() {
    std::vector<ScreenInfo> out;
    const auto screens = QGuiApplication::screens();
    for (auto* s : screens) {
        out.push_back(ScreenInfo{s->geometry().size(), s->devicePixelRatio(),
                                 s == QGuiApplication::primaryScreen(), s->name()});
    }
    return out;
}

} // namespace

AppShell::AppShell(QObject* parent) : QObject(parent) {
    registerPresentMetaTypes();
    clock_.start();
    // The quit prompt must time out on its own rather than sitting unanswered on the
    // projector; PresentationController::onTick had no caller at all (audit H4).
    tick_ = new QTimer(this);
    tick_->setInterval(250);
    connect(tick_, &QTimer::timeout, this, [this] {
        const Mode before = controller_.mode();
        controller_.onTick(clock_.elapsed());
        if (controller_.mode() != before) {
            refresh();
        }
    });
    tick_->start();
}

AppShell::~AppShell() {
    teardownWorkers();
}

void AppShell::showStart() {
    if (!start_) {
        start_ = new StartView();
    }
    start_->show();
}

void AppShell::teardownWorkers() {
    // Bounded shutdown: cancel, quit, wait. The app must never hang on exit because
    // a worker is mid-slide.
    if (loadWorker_) {
        loadWorker_->cancel();
    }
    if (renderWorker_) {
        renderWorker_->cancel();
    }
    for (QPointer<QThread> t : {loadThread_, renderThread_}) {
        if (!t) {
            continue;
        }
        t->quit();
        // BOUNDED shutdown (audit H2). quit() cannot interrupt the render loop —
        // that is not an event loop — and a single legal slide can take minutes, so
        // waiting forever would hang the app on exit. If the worker will not stop,
        // terminate rather than destroy a running QThread (which is a qFatal abort).
        if (!t->wait(5000)) {
            // A single legal slide can render for minutes (BUG-21), so the wait can
            // genuinely expire. terminate() is unsafe (it can strand allocator locks)
            // and destroying a running QThread is a qFatal ABORT — which is what the
            // previous H2 fix still risked (UAT-3 SEV-2 #3). Deliberately DETACH and
            // leak it instead: the worker owns nothing the app needs back, and a
            // leaked thread at shutdown is strictly better than aborting in front of
            // a room. Its parent is cleared so ~QObject cannot destroy it either.
            t->setParent(nullptr);
            continue;
        }
        t->deleteLater();
    }
    loadThread_.clear();
    loadWorker_.clear();
    renderThread_.clear();
    renderWorker_.clear();
}

void AppShell::openDeck(const QString& path) {
    teardownWorkers();

    loadThread_ = new QThread(this);
    loadWorker_ = new DeckLoadWorker();
    loadWorker_->setPath(path);
    loadWorker_->setLoadFn([](const QString& p) { return DeckLoader::load(p); });
    loadWorker_->moveToThread(loadThread_);
    connect(loadThread_, &QThread::started, loadWorker_, &DeckLoadWorker::start);
    connect(loadWorker_, &DeckLoadWorker::loaded, this, &AppShell::onDeckLoaded);
    connect(loadWorker_, &DeckLoadWorker::finished, loadThread_, &QThread::quit);
    connect(loadThread_, &QThread::finished, loadWorker_, &QObject::deleteLater);
    loadThread_->start();
}

void AppShell::onDeckLoaded(DeckLoadOutcome outcome) {
    if (!outcome.ok || !outcome.presentation) {
        // Show a FIXED string chosen by kind. LoadError::message embeds the deck's
        // full path (and, for a hostile archive, attacker-controlled bytes), and this
        // dialog can land on the projector — Bible section 8 / TM-013 (audit C3/C4).
        QMessageBox::warning(nullptr, QStringLiteral("Could not open the deck"),
                             describeLoadError(outcome.error.kind));
        // showStart() CREATES the view if it does not exist. The old `if (start_)`
        // was dead on the CLI launch path (start_ is null there), so a failed open
        // left no window at all and the app silently exited (UAT-3 SEV-2 #5).
        showStart();
        return;
    }
    deck_ = outcome.presentation;
    const int count = static_cast<int>(deck_->slides.size());
    if (count == 0) {
        // A deck that parsed but has no slides must NOT go fullscreen: Mode stays
        // Idle, so Esc can never reach the quit prompt and the window could not be
        // closed at all — an unquittable black rectangle on the projector (audit C2).
        QMessageBox::warning(nullptr, QStringLiteral("Could not open the deck"),
                             QStringLiteral("That file contains no slides."));
        showStart();
        return;
    }
    controller_.setDeck(count);
    rasters_.assign(static_cast<std::size_t>(count), QImage());

    if (!window_) {
        window_ = new PresentationWindow(&controller_);
        window_->setCommandSink([this](Command c) {
            applyResult(controller_.dispatch(c, CommandSource::Keyboard, false));
        });
        window_->setUiRequestSink([this](UiRequest r) {
            switch (r) {
            case UiRequest::RequestHolding:
            case UiRequest::RequestQuitConfirm:
                controller_.requestHolding(clock_.elapsed());
                refresh();
                break;
            case UiRequest::CancelQuit:
                controller_.cancelQuit();
                refresh();
                break;
            case UiRequest::MoveSlideWindowToNextScreen:
                moveWindowToNextScreen();
                break;
            case UiRequest::ConfirmQuit:
                controller_.confirmQuit();
                if (window_) {
                    window_->close();
                }
                break;
            default:
                break;
            }
        });
    }

    // Pre-render every slide off the UI thread BEFORE the talk (TM-018), starting at
    // the slide being shown so presenting can begin immediately.
    renderThread_ = new QThread(this);
    renderWorker_ = new PreRenderWorker();
    renderWorker_->setDeck(deck_);
    // Render at the DECK's aspect ratio, sized for the screen we will present on.
    // Previously the target was the largest screen by DEVICE pixels — on a Retina
    // laptop plus a 1080p projector that is the LAPTOP (3024x1964, aspect 1.54), and
    // SlideRenderer bakes letterbox bars INTO the raster at the target aspect. The
    // surface then letterboxed that raster AGAIN against the 16:9 window, so the deck
    // covered only 75% of the projector with 13% smaller text, for the whole talk —
    // and it is invisible unless a second screen of a different aspect is attached
    // (UAT-3 SEV-2 #2). Matching the raster aspect to the deck removes the first
    // letterbox entirely.
    renderWorker_->setTarget(
        renderTargetForDeck(currentScreens(), QSize(static_cast<int>(deck_->slideWidth),
                                                    static_cast<int>(deck_->slideHeight))));
    // The renderer needs the deck's slide dimensions (EMU) to scale correctly; they
    // are captured BY VALUE so the lambda stays valid on the worker thread.
    const Emu slideW = deck_->slideWidth;
    const Emu slideH = deck_->slideHeight;
    renderWorker_->setRenderFn([slideW, slideH](const Slide& s, const QSize& target) {
        return SlideRenderer::render(s, slideW, slideH, target.width(), target.height());
    });
    renderWorker_->setPlaceholderFn([](int, const QSize& target) {
        QImage img(target.isEmpty() ? QSize(1280, 720) : target, QImage::Format_RGB32);
        img.fill(QColor(32, 32, 32));
        return img;
    });
    renderWorker_->moveToThread(renderThread_);
    connect(renderThread_, &QThread::started, renderWorker_, &PreRenderWorker::start);
    connect(renderWorker_, &PreRenderWorker::slideReady, this, &AppShell::onSlideReady);
    connect(renderWorker_, &PreRenderWorker::finished, renderThread_, &QThread::quit);
    connect(renderThread_, &QThread::finished, renderWorker_, &QObject::deleteLater);
    // NOTE: renderThread_->start() is deliberately NOT called here — see the end of
    // this function (BUG-30). Starting it before the window exists put the worker
    // inside Qt's font database at the exact moment showing the window made the GUI
    // thread rebuild the theme, and that data race SEGV'd the app on Karl's deck.

    if (start_) {
        start_->hide();
    }
    // Present on the EXTERNAL display when there is one — the projector is almost
    // never the primary screen (audit H5). Ctrl+Shift+D moves it if this guesses wrong.
    const auto screens = QGuiApplication::screens();
    for (QScreen* sc : screens) {
        if (sc != QGuiApplication::primaryScreen()) {
            window_->setScreen(sc);
            window_->setGeometry(sc->geometry());
            break;
        }
    }
    window_->showFullScreen();
    window_->raise();
    window_->activateWindow();
    window_->setFocus();
    refresh();

    // BUG-30 — only NOW start rendering. Creating and showing a widget window is
    // what makes the GUI thread run QApplicationPrivate::handleThemeChanged(), and
    // that rewrites the font state the renderer reads. Karl's crash report caught
    // the two threads in exactly that pair of call stacks. Deferring the start
    // through the event loop lets the theme settle first, so the worker never
    // overlaps the one window we know triggers it.
    //
    // This narrows the race to spontaneous theme changes (macOS switching to dark
    // mode at sunset, a display being attached, a remote-desktop session
    // reconnecting) that land WHILE the deck is still pre-rendering. For a 10-slide
    // deck that window is ~0.2 s; it grows with deck size. Closing it completely
    // means not touching Qt's font database off the GUI thread at all — tracked as
    // BUG-34 for F7c.
    QMetaObject::invokeMethod(
        this,
        [this]() {
            if (renderThread_ && !renderThread_->isRunning()) {
                renderThread_->start();
            }
        },
        Qt::QueuedConnection);
}

void AppShell::onSlideReady(int index, QImage image, bool /*isPlaceholder*/) {
    // The single QImage -> QPixmap-eligible hand-off point, on the GUI thread, per
    // the ratified amendment A3-1(1).
    if (index >= 0 && index < static_cast<int>(rasters_.size())) {
        rasters_[static_cast<std::size_t>(index)] = image;
    }
    if (index == controller_.currentIndex0Based()) {
        // refresh(), NOT showSlide(): refresh is the only mode-aware path. Calling
        // showSlide here painted the deck straight back onto a BLANKED projector
        // 1-3 s after Esc, and wiped the quit prompt (UAT-3 SEV-1). The previous H3
        // fix gated refresh() but left this raster path ungated — an incomplete fix.
        refresh();
    }
}

void AppShell::refresh() {
    if (!window_) {
        return;
    }
    switch (controller_.mode()) {
    case Mode::Holding:
        // BLANK the projector. This is the whole point of the privacy screen, and it
        // was never implemented: Esc changed the mode and left the deck on the wall.
        window_->setSlideImage(QImage());
        window_->surface()->setStatusText(
            noticeForRole(Notice{NoticeId::HoldingHint}, NoticeRole::Operator, false));
        window_->setNotice(QString());
        return;
    case Mode::ConfirmQuit:
        // The prompt must be VISIBLE — it swallows every key, so an invisible one
        // reads as a frozen app.
        window_->setSlideImage(QImage());
        window_->surface()->setStatusText(
            QStringLiteral("Quit the presentation?  Ctrl+Shift+Q to quit  ·  Esc to go back"));
        window_->setNotice(QString());
        return;
    case Mode::Idle:
    case Mode::Presenting:
        break;
    }
    window_->setNotice(lastNotice_);
    showSlide(controller_.currentSlide1Based());
}

void AppShell::moveWindowToNextScreen() {
    // The documented escape hatch when the fullscreen window opens on the laptop
    // panel instead of the projector (audit H5) — it previously did nothing.
    const auto screens = QGuiApplication::screens();
    if (!window_ || screens.size() < 2) {
        return;
    }
    QScreen* const cur = window_->screen();
    int idx = static_cast<int>(screens.indexOf(cur));
    QScreen* const next = screens.at((idx + 1) % screens.size());
    window_->showNormal();
    window_->setScreen(next);
    window_->setGeometry(next->geometry());
    window_->showFullScreen();
    window_->raise();
    window_->activateWindow();
    window_->setFocus();
}

void AppShell::showSlide(int index1Based) {
    if (!window_ || index1Based < 1) {
        return;
    }
    // Belt and braces: this is the single writer of the surface image, so it must
    // never paint while the deck is deliberately hidden (UAT-3 SEV-1).
    if (controller_.mode() != Mode::Presenting) {
        return;
    }
    const std::size_t i = static_cast<std::size_t>(index1Based - 1);
    if (i < rasters_.size() && !rasters_[i].isNull()) {
        window_->setSlideImage(rasters_[i]);
    } else {
        window_->setSlideImage(QImage());
        window_->surface()->setStatusText(QStringLiteral("Rendering slide %1...").arg(index1Based));
    }
    if (renderWorker_) {
        // Tell the renderer what the presenter is looking at, so it re-steers.
        QMetaObject::invokeMethod(renderWorker_, "setCurrentIndex", Qt::QueuedConnection,
                                  Q_ARG(int, index1Based - 1));
    }
}

void AppShell::applyResult(const DispatchResult& r) {
    if (!window_) {
        return;
    }
    // AUDIENCE role: this window is the one on the projector. The operator-only
    // surface arrives with F5 (audit M2).
    lastNotice_ = noticeForRole(r.notice, NoticeRole::Audience, false);
    refresh();
    if (controller_.quitConfirmed()) {
        window_->close();
    }
}

} // namespace pptv
