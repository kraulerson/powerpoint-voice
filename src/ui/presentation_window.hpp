#pragma once

#include <functional>

#include <QElapsedTimer>
#include <QWidget>

#include "present/key_translator.hpp"
#include "present/presentation_controller.hpp"

class QCloseEvent;

namespace pptv {

class SlideSurface;
class NoticeStrip;

// The fullscreen presentation window (Feature F7b).
//
// It owns no policy: keys go to the pure KeyCommandTranslator, commands go to the
// pure PresentationController, and the window only applies the result. Two
// behaviours are its own responsibility, and both protect a live talk:
//   * Esc must NOT close it. QWidget's DEFAULT is to close on Esc — which would end
//     the presentation with a single keypress.
//   * A close request is REFUSED unless quitting has actually been confirmed.
class PresentationWindow : public QWidget {
    Q_OBJECT

  public:
    explicit PresentationWindow(PresentationController* controller, QWidget* parent = nullptr);

    void setSlideImage(const QImage& img);
    void setNotice(const QString& text);
    SlideSurface* surface() const { return surface_; }
    NoticeStrip* strip() const { return strip_; }

    // Set by the app shell; receives every command the keyboard produces.
    void setCommandSink(std::function<void(Command)> sink) { sink_ = std::move(sink); }
    void setUiRequestSink(std::function<void(UiRequest)> sink) { uiSink_ = std::move(sink); }
    // The recognizer is the single owner of pause; the window only mirrors it.
    void setPaused(bool paused) { paused_ = paused; }
    qint64 nowMs() const;

  protected:
    void keyPressEvent(QKeyEvent* e) override;
    void closeEvent(QCloseEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;

  private:
    PresentationController* controller_ = nullptr;
    KeyCommandTranslator translator_;
    SlideSurface* surface_ = nullptr;
    NoticeStrip* strip_ = nullptr;
    std::function<void(Command)> sink_;
    std::function<void(UiRequest)> uiSink_;
    // MONOTONIC, as PresentationController::requestHolding documents. Passing 0 (as
    // this did) made the 3 s typed-number staleness rule and the quit-prompt timeout
    // structurally unreachable (audit M1/H4).
    QElapsedTimer clock_;
    Mode lastMode_ = Mode::Idle;
    bool paused_ = false;
};

} // namespace pptv
