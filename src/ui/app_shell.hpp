#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QString>
#include <memory>

#include "audio/voice_pipeline.hpp"
#include "command/recognizer_controller.hpp"
#include <vector>

#include <QImage>

#include "present/deck_load_worker.hpp"
#include "present/pre_render_worker.hpp"
#include "present/presentation_controller.hpp"

class QThread;
class QTimer;

namespace pptv {

class PresentationWindow;
class StartView;

// Wires the pieces together (Feature F7b). It holds NO policy: every decision comes
// from pptv_core (PresentationController, the pure geometry and key layers), and this
// class only moves data between them, the workers and the window.
class AppShell : public QObject {
    Q_OBJECT

  public:
    // Test seams for BUG-60. The application's WIRING had no tests, so three
    // mutations could restore the "no way to open a deck" state with the whole
    // suite green. These expose enough to assert the wires exist, and nothing more.
    StartView* startViewForTest() const { return start_; }

    // Test seams for F-2 — the P key was DEAD and the whole suite stayed green,
    // because the wiring that gives it meaning lives in this class and this class
    // could not be driven without a microphone and a deck. These install the two
    // halves of that wiring against a caller-owned window, and expose the one bit of
    // state the key is supposed to move. Nothing else.
    void installVoiceGateForTest() { installVoiceGate(); }
    // Adopts a caller-owned window as THE window and wires it exactly as openDeck
    // would. The caller must outlive the shell.
    void installWindowSinksForTest(PresentationWindow* w) {
        window_ = w;
        installWindowSinks(w);
    }
    bool voiceGatePausedForTest() const;

    // Test seams for F-CHAOS-2. A raster arriving from an ABANDONED worker is not
    // reachable from outside — abandoning one requires a render that outruns the
    // teardown wait. These drive the guard directly instead.
    int deckGenerationForTest() const { return deckGeneration_; }
    void openDeckGenerationForTest(int slideCount);
    void deliverSlideForTest(int generation, int index, const QImage& image) {
        acceptSlide(generation, index, image, false);
    }
    bool hasRasterForTest(int index) const;

  signals:
    // Emitted whenever a deck open is ATTEMPTED, whatever the outcome. Its only
    // purpose is to make the start-screen wiring observable.
    void deckOpenAttempted(const QString& path);

  public:
    explicit AppShell(QObject* parent = nullptr);
    ~AppShell() override;

    void showStart();
    // Open a deck: parse off-thread, then pre-render off-thread, then present.
    void openDeck(const QString& path);
    // Asks the user for a deck, then opens it. Separate from openDeck so the load
    // path stays testable without a modal dialog.
    void browseForDeck();
    // Arms voice if it can be armed safely. Returns the reason it could not, and
    // NEVER prevents the presentation from running — the keyboard is the guaranteed
    // control path (F8b audit F8b-6, F8c audit F8c-4).
    QString armVoice();

  private slots:
    void onDeckLoaded(DeckLoadOutcome outcome);
    void onSlideReady(int index, QImage image, bool isPlaceholder);

  private:
    // Generation gate in front of onSlideReady (F-CHAOS-2). A raster from an
    // abandoned worker belonging to a previous deck is dropped rather than written
    // into the current deck's raster slots.
    void acceptSlide(int generation, int index, const QImage& image, bool isPlaceholder);

  private:
    void applyResult(const DispatchResult& r);
    // Repaints the surface from the CURRENT mode. This is what makes the privacy
    // blackout and the quit prompt actually visible (audit H3/H4) — previously the
    // mode changed and nothing on screen followed.
    void refresh();
    void moveWindowToNextScreen();
    void showSlide(int index1Based);
    // Creates the recogniser gate. Separate from armVoice() so the gate can exist
    // without a microphone — the keyboard reaches it too (F-2).
    void installVoiceGate();
    // Installs the command and UI-request sinks on a presentation window.
    void installWindowSinks(PresentationWindow* w);
    void teardownWorkers();
    // Stops the microphone and releases the voice trio, in the ONE order that is
    // safe. See the definition and the member declarations below for why.
    void teardownVoice();

    PresentationController controller_;
    PresentationWindow* window_ = nullptr;
    StartView* start_ = nullptr;

    // QPointer, NOT a raw pointer: Qt destroys these workers via deleteLater() on
    // QThread::finished, which fires on the WORKER thread with the worker as a
    // direct receiver — so the object is gone the moment pre-render completes, and a
    // raw pointer dangles. Dereferencing it (showSlide -> invokeMethod) was a
    // reproducible SEGV on the first key press after load (audit C1). QPointer
    // self-nulls, so every `if (worker_)` guard in this file becomes truthful.
    QPointer<QThread> loadThread_;
    QPointer<DeckLoadWorker> loadWorker_;
    QPointer<QThread> renderThread_;
    QPointer<PreRenderWorker> renderWorker_;
    // Voice. Owned here because it must die with the shell, and null until armed.
    //
    // DECLARATION ORDER IS LOAD-BEARING (F-1). The audio callback runs on CoreAudio's
    // real-time thread and reaches, in this order: VoicePipeline -> VoskEngine::feed
    // -> (queued) RecognizerController. So the pipeline — the only one of the three
    // that can STOP that thread — has to be the last one standing, and members are
    // destroyed in REVERSE declaration order. Declaring the engine and the gate first
    // means `voice_` is torn down first, which is what makes the other two safe to
    // free.
    //
    // Before this order, `~AppShell` freed VoskEngine while the capture thread was
    // still inside `feed()`. A Phase 3 reviewer caught both stacks under
    // AddressSanitizer and reproduced it 7 times out of 7. It never fired on the
    // development Mac mini, which has no input device — it fires on the presenter's
    // MacBook Pro, where voice actually runs, on every Cmd+Q at the end of a talk.
    //
    // `teardownVoice()` performs the same order explicitly at the top of the
    // destructor. Two mechanisms deliberately: deleting either one leaves the app
    // correct, and the comment above each says what the other is for.
    std::unique_ptr<VoskEngine> engine_;
    std::unique_ptr<RecognizerController> voiceGate_;
    std::unique_ptr<VoicePipeline> voice_;
    // Why voice is off, if it is. Shown to the OPERATOR; never to the audience.
    QString voiceUnavailableReason_;

  public:
    QString voiceUnavailableReason() const { return voiceUnavailableReason_; }
    bool voiceIsRunning() const { return voice_ && voice_->isRunning(); }

  private:
    PresentationPtr deck_;
    // Bumped on every openDeck. Anything a worker emits carrying an older generation
    // belongs to a deck the presenter has moved on from (F-CHAOS-2).
    int deckGeneration_ = 0;
    std::vector<QImage> rasters_;
    QTimer* tick_ = nullptr; // drives the quit-prompt auto-dismiss (audit H4)
    QElapsedTimer clock_;    // monotonic, per the controller's contract
    QString lastNotice_;
};

} // namespace pptv
