#include "ui/app_shell.hpp"

#include <QApplication>
#include <QFileDialog>
#include <QMessageBox>
#include <QScreen>
#include <QThread>
#include <QTimer>

#include "audio/miniaudio_capture.hpp"
#include "command/vosk_recognizer.hpp"
#include "loader/deck_loader.hpp"
#include "present/display_geometry.hpp"
#include "present/raster_cache.hpp"
#include "render/slide_renderer.hpp"
#include "ui/presentation_window.hpp"
#include <unistd.h>

#include "ui/quit_policy.hpp"
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
    // Voice FIRST, before anything else and before any member is released (F-1).
    // The microphone is the only input still arriving at this point: the deck and
    // render workers are ours to cancel, but CoreAudio's real-time thread is not,
    // and it is calling into objects this destructor is about to free.
    teardownVoice();
    teardownWorkers();
}

bool AppShell::voiceGatePausedForTest() const {
    return voiceGate_ && voiceGate_->state() == RecognizerController::State::Paused;
}

void AppShell::openDeckGenerationForTest(int slideCount) {
    // The two things openDeck() does that matter to the generation guard, without a
    // file, a thread or a window: a new generation and a raster set sized for it.
    ++deckGeneration_;
    controller_.setDeck(slideCount);
    rasters_.assign(static_cast<std::size_t>(slideCount < 0 ? 0 : slideCount), QImage());
}

bool AppShell::hasRasterForTest(int index) const {
    if (index < 0 || index >= static_cast<int>(rasters_.size())) {
        return false;
    }
    return !rasters_[static_cast<std::size_t>(index)].isNull();
}

void AppShell::teardownVoice() {
    // One order, and only one. VoicePipeline::stop() stops the DEVICE, and
    // ma_device_stop() does not return until the audio callback has finished — so
    // once the pipeline is gone, nothing can be inside the decoder, and only then
    // is the engine the decoder calls into safe to free.
    //
    // Doing it the other way round frees VoskEngine underneath a live `feed()`.
    // That is not theoretical: a Phase 3 reviewer caught it under AddressSanitizer
    // with both stacks, 7 reproductions out of 7. It is invisible on a machine with
    // no microphone, which is exactly why it survived to Phase 3 here.
    if (voice_) {
        voice_->stop();
    }
    voice_.reset();
    engine_.reset();
    voiceGate_.reset();
}

void AppShell::showStart() {
    if (!start_) {
        start_ = new StartView();
        // The view offers the ways in; choosing and loading the file is ours
        // (BUG-18 — it shipped with neither, so the app could only be driven from
        // the command line and was unusable to anyone who launched it normally).
        connect(start_, &StartView::browseRequested, this, &AppShell::browseForDeck);
        connect(start_, &StartView::fileDropped, this, &AppShell::openDeck);
    }
    start_->show();
    start_->raise();
    start_->activateWindow();
}

void AppShell::installVoiceGate() {
    // The gate the whole command layer was built around: it decides whether a heard
    // phrase becomes a command, and it is the single owner of Paused.
    voiceGate_ = std::make_unique<RecognizerController>([this](Command c) {
        // Keep the KEYBOARD's view of the pause state in step with the gate's, so P
        // stays a true toggle after a SPOKEN "pause presentation" (F-2). The gate
        // commits its state before calling this sink — documented contract — so
        // state() here is already the new one.
        if (window_ && voiceGate_) {
            window_->setPaused(voiceGate_->state() == RecognizerController::State::Paused);
        }
        applyResult(controller_.dispatch(c, CommandSource::Voice, false));
    });
}

QString AppShell::armVoice() {
    // Order matters: validate the model and grammar BEFORE opening the microphone,
    // so a model that cannot constrain never causes a permission prompt for a
    // capability we are about to refuse to use.
    const RecognizerSetup setup = prepareRecognizer(resolveModelDir());
    if (setup.error != RecognizerInitError::None) {
        return QString::fromUtf8(describeRecognizerInitError(setup.error));
    }
    engine_ = std::make_unique<VoskEngine>();
    const RecognizerInitError err = engine_->start(setup);
    if (err != RecognizerInitError::None) {
        engine_.reset();
        return QString::fromUtf8(describeRecognizerInitError(err));
    }

    installVoiceGate();

    voice_ = std::make_unique<VoicePipeline>(this);
    voice_->setCapture(makeMiniaudioCapture());
    voice_->setDecoder([this](const std::int16_t* s, std::size_t n) {
        return engine_ ? engine_->feed(s, n) : QString();
    });
    // Heard text crosses to the GUI thread here and goes STRAIGHT into the gate.
    // It is never stored, never logged, and never rendered (Bible section 8,
    // TM-012/013) — the F5 transcript overlay is a separate, deliberate decision.
    connect(voice_.get(), &VoicePipeline::phraseHeard, this, [this](const QString& phrase) {
        if (voiceGate_) {
            voiceGate_->onPhrase(phrase);
        }
    });

    const CaptureError cerr = voice_->start();
    if (cerr != CaptureError::None) {
        voice_.reset();
        engine_.reset();
        voiceGate_.reset();
        return QString::fromUtf8(describeCaptureError(cerr));
    }
    return QString();
}

void AppShell::installWindowSinks(PresentationWindow* w) {
    if (w == nullptr) {
        return;
    }
    // QPointer, not a raw capture: the sink outlives nothing in particular, and a
    // window destroyed under it must read as absent rather than as a dangling write.
    const QPointer<PresentationWindow> target(w);
    w->setCommandSink([this, target](Command c) {
        // Pause and continue are not slide movements, so PresentationController
        // treats them as no-ops. Their real effect lives in the recogniser gate —
        // which means the KEYBOARD has to reach through the gate too, or P does
        // absolutely nothing (F-2). It did nothing: PresentationWindow carried a
        // `paused_` flag with no writer, so P always translated to
        // PausePresentation, and that command changed nothing anywhere. A presenter
        // pressing P before taking questions would have believed voice was gated
        // while it was still fully live — the failure this key exists to prevent.
        if (c.type == CommandType::PausePresentation ||
            c.type == CommandType::ContinuePresentation) {
            // With voice not armed there is nothing to gate, and reporting "Resumed"
            // about a subsystem that is not running is a lie the presenter would act
            // on. So P is silently inert in that case rather than falsely reassuring.
            if (!voiceGate_) {
                return;
            }
            const bool wantPaused = (c.type == CommandType::PausePresentation);
            voiceGate_->setPaused(wantPaused);
            if (target) {
                target->setPaused(wantPaused);
            }
        }
        applyResult(controller_.dispatch(c, CommandSource::Keyboard, false));
    });
    w->setUiRequestSink([this](UiRequest r) {
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

void AppShell::browseForDeck() {
    const QString path =
        QFileDialog::getOpenFileName(start_, tr("Open presentation"), QString(),
                                     tr("PowerPoint presentations (*.pptx);;All files (*)"));
    if (!path.isEmpty()) {
        openDeck(path);
    }
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
    // CUT THE WIRES BACK TO US, before anything else (F-CHAOS-2). A worker whose wait
    // expires is deliberately abandoned rather than terminated (see below) — and an
    // abandoned worker keeps rendering. Its slideReady is still connected here, so
    // slides from the PREVIOUS deck arrive after openDeck() has resized rasters_ for
    // the new one and land in the new deck's slots. The projector then shows the old
    // deck's content under the new deck's slide numbers, silently.
    //
    // Belt and braces with the generation check in acceptSlide(): disconnect stops
    // the connection, the generation stops anything Qt has already queued.
    if (loadWorker_) {
        disconnect(loadWorker_, nullptr, this, nullptr);
    }
    if (renderWorker_) {
        disconnect(renderWorker_, nullptr, this, nullptr);
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
        // On the QUIT path a long wait is worse than useless: exec() has already
        // returned, so the GUI thread has NO RUNLOOP while it blocks here and macOS
        // never re-invokes applicationShouldTerminate:. Every further Dock or
        // Activity Monitor quit in that window is silently discarded, which from the
        // outside is indistinguishable from the app refusing to die (BUG-42).
        //
        // The tempting fix — skip the wait and detach — was MEASURED WRONG by an
        // adversarial reviewer: 8 of 14 runs SEGV, because ~QGuiApplication tears
        // down the font database while the render thread is still inside
        // QFont/QPainter. Their third variant, _exit(0), was clean in 6 of 6.
        //
        // So: wait briefly, and if the worker is genuinely mid-slide, leave by the
        // one door that cannot race a destructor. _exit skips static destructors and
        // atexit handlers, which is safe here precisely because this application
        // writes nothing — TM-011 forbids a disk cache, so there is nothing to flush.
        const int waitMs = applicationIsTerminating() ? 300 : 5000;
        if (!t->wait(waitMs)) {
            if (applicationIsTerminating()) {
                ::_exit(0);
            }
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
    emit deckOpenAttempted(path);
    teardownWorkers();
    // A new deck is a new generation. Everything an abandoned worker of a previous
    // generation still emits is now stale by definition (F-CHAOS-2).
    ++deckGeneration_;

    loadThread_ = new QThread(this);
    loadWorker_ = new DeckLoadWorker();
    loadWorker_->setPath(path);
    loadWorker_->setLoadFn([](const QString& p) { return DeckLoader::load(p); });
    loadWorker_->moveToThread(loadThread_);
    const int gen = deckGeneration_;
    connect(loadThread_, &QThread::started, loadWorker_, &DeckLoadWorker::start);
    connect(loadWorker_, &DeckLoadWorker::loaded, this, [this, gen](DeckLoadOutcome outcome) {
        if (gen != deckGeneration_) {
            return; // a deck the presenter has already moved on from
        }
        onDeckLoaded(std::move(outcome));
    });
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
        installWindowSinks(window_);
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
    const int gen = deckGeneration_;
    connect(renderWorker_, &PreRenderWorker::slideReady, this,
            [this, gen](int index, const QImage& image, bool isPlaceholder) {
                acceptSlide(gen, index, image, isPlaceholder);
            });
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
            // Arm voice AFTER the window exists and rendering is under way, so the
            // microphone permission prompt cannot land on a bare screen and so a
            // voice failure can never delay the deck appearing. A failure is
            // reported and ignored: the keyboard drives everything regardless.
            const QString why = armVoice();
            if (!why.isEmpty()) {
                voiceUnavailableReason_ = why;
            }
        },
        Qt::QueuedConnection);
}

void AppShell::acceptSlide(int generation, int index, const QImage& image, bool isPlaceholder) {
    // The generation check (F-CHAOS-2). A render worker whose wait expired during
    // teardown is ABANDONED, not stopped — that is deliberate (see teardownWorkers),
    // and it means the old worker is still rasterising the OLD deck while the new one
    // is being loaded. Its slides would land in rasters_, which openDeck has already
    // resized for the new deck, and the projector would show the previous deck's
    // content under the new deck's slide numbers. Silently: every index is in range.
    if (generation != deckGeneration_) {
        return;
    }
    onSlideReady(index, image, isPlaceholder);
}

void AppShell::onSlideReady(int index, QImage image, bool /*isPlaceholder*/) {
    // The single QImage -> QPixmap-eligible hand-off point, on the GUI thread, per
    // the ratified amendment A3-1(1).
    if (index >= 0 && index < static_cast<int>(rasters_.size())) {
        rasters_[static_cast<std::size_t>(index)] = image;
        // Keep the raster window inside its budget (BUG-22). Unbounded, a 300-slide
        // 4K deck is ~9.27 GB and the machine swaps then dies mid-talk.
        std::vector<std::size_t> sizes;
        sizes.reserve(rasters_.size());
        for (const QImage& r : rasters_) {
            sizes.push_back(rasterBytes(r));
        }
        for (int victim :
             evictionOrder(sizes, controller_.currentIndex0Based(), kRasterBudgetBytes)) {
            rasters_[static_cast<std::size_t>(victim)] = QImage();
        }
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
        window_->surface()->setStatusText(quitConfirmHint());
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
