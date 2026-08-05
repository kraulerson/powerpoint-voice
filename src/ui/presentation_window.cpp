#include "ui/presentation_window.hpp"

#include "ui/quit_policy.hpp"

#include <QCloseEvent>
#include <QKeyEvent>
#include <QResizeEvent>

#include "ui/notice_strip.hpp"
#include "ui/slide_surface.hpp"

namespace pptv {

PresentationWindow::PresentationWindow(PresentationController* controller, QWidget* parent)
    : QWidget(parent), controller_(controller) {
    // The window is what refuses close requests, so it is what must also recognise
    // an application-level quit and stand aside for it (BUG-31).
    installApplicationQuitFilter();
    surface_ = new SlideSurface(this);
    strip_ = new NoticeStrip(this);
    setFocusPolicy(Qt::StrongFocus);
    QPalette p = palette();
    p.setColor(QPalette::Window, Qt::black);
    setPalette(p);
    setAutoFillBackground(true);
    clock_.start();
}

qint64 PresentationWindow::nowMs() const {
    return clock_.isValid() ? clock_.elapsed() : 0;
}

void PresentationWindow::setSlideImage(const QImage& img) {
    surface_->setSlideImage(img);
}

void PresentationWindow::setNotice(const QString& text) {
    strip_->setText(text);
}

void PresentationWindow::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    const int stripH = NoticeStrip::heightFor(height());
    surface_->setGeometry(0, 0, width(), height());
    strip_->setGeometry(0, height() - stripH, width(), stripH);
    strip_->raise();
}

void PresentationWindow::keyPressEvent(QKeyEvent* e) {
    const Mode mode = controller_ ? controller_->mode() : Mode::Presenting;
    if (mode != lastMode_) {
        // Clear any half-typed slide number whenever the mode changes, so it cannot
        // fire after the presenter has moved on (audit M1).
        translator_.onModeChanged(mode);
        lastMode_ = mode;
    }
    const KeyContext ctx{mode, paused_};
    const KeyAction a = translator_.onKey(e->key(), e->modifiers(), ctx, nowMs());

    if (a.command && sink_) {
        sink_(*a.command);
    }
    if (a.uiRequest && uiSink_) {
        uiSink_(*a.uiRequest);
    }
    if (a.consumed) {
        // Accept and RETURN without calling the base class. This is what stops Esc
        // (and any other handled key) from reaching Qt's default handling, which for
        // some widget types closes the window — i.e. ends the talk on one keypress.
        e->accept();
        return;
    }
    QWidget::keyPressEvent(e);
}

void PresentationWindow::closeEvent(QCloseEvent* e) {
    // A close request — the window button, Cmd-Q, Quit from the Dock — must not end a
    // presentation outright. But it must not be SILENTLY SWALLOWED either: doing that
    // made the app impossible to quit by any normal means, and Karl had to force-quit
    // from Activity Monitor (UAT-3 human run). So a close request now RAISES THE QUIT
    // PROMPT, which is the same deliberate two-step the keyboard uses, and the user
    // gets a visible way out.
    // ...unless the APPLICATION is being quit. Dock -> Quit, Activity Monitor ->
    // Quit and Cmd+Q all arrive as a close on every top-level window, and Qt cancels
    // the whole shutdown if any window refuses — which is what left the tester with
    // Force Quit as his only exit. That request came from outside the app and is
    // already deliberate, so it is obeyed from any mode, with no prompt (BUG-31).
    if (applicationQuitInProgress()) {
        e->accept();
        return;
    }
    if (controller_ && !controller_->quitConfirmed()) {
        e->ignore();
        if (uiSink_) {
            uiSink_(controller_->mode() == Mode::Holding ? UiRequest::RequestQuitConfirm
                                                         : UiRequest::RequestHolding);
        }
        return;
    }
    e->accept();
}

} // namespace pptv
