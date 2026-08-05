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
    void applyResult(const DispatchResult& r);
    // Repaints the surface from the CURRENT mode. This is what makes the privacy
    // blackout and the quit prompt actually visible (audit H3/H4) — previously the
    // mode changed and nothing on screen followed.
    void refresh();
    void moveWindowToNextScreen();
    void showSlide(int index1Based);
    void teardownWorkers();

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
    std::unique_ptr<VoicePipeline> voice_;
    std::unique_ptr<VoskEngine> engine_;
    std::unique_ptr<RecognizerController> voiceGate_;
    // Why voice is off, if it is. Shown to the OPERATOR; never to the audience.
    QString voiceUnavailableReason_;

  public:
    QString voiceUnavailableReason() const { return voiceUnavailableReason_; }
    bool voiceIsRunning() const { return voice_ && voice_->isRunning(); }

  private:
    PresentationPtr deck_;
    std::vector<QImage> rasters_;
    QTimer* tick_ = nullptr; // drives the quit-prompt auto-dismiss (audit H4)
    QElapsedTimer clock_;    // monotonic, per the controller's contract
    QString lastNotice_;
};

} // namespace pptv
