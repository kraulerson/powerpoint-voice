#pragma once

#include <QWidget>

class QLabel;
class QMimeData;
class QPushButton;

namespace pptv {

// The dark landing surface shown before a deck is loaded (Project Bible §9).
//
// It owns NO policy: it offers the ways in and reports what the user did. Choosing
// a file, validating it and loading it all belong to AppShell, so this stays a
// widget that can be tested without a file dialog.
//
// It shipped as two labels and nothing else (BUG-18), which made the application
// unusable by anyone who launched it normally — the deck could only be supplied as
// a command-line argument, and double-clicking the app left you with a dark window
// and no way forward. Karl hit exactly that on the first build he was given.
// The drop decision, as a pure function so it is testable without Qt's drag-and-drop
// machinery (a synthetic QDropEvent is not dispatched by QWidget::event, so testing
// through the widget tests the harness rather than the rule).
//
// Only a LOCAL file is usable. A remote URL would have to be fetched, and this
// application does not touch the network at all — accepting one would create the
// project's first network path by accident, in a drop handler.
QString localDeckPathFrom(const QMimeData* mime);

class StartView : public QWidget {
    Q_OBJECT

  public:
    explicit StartView(QWidget* parent = nullptr);

    // Test seam: the button the user clicks to browse.
    QPushButton* openButton() const { return m_open; }
    // Test seam for BUG-60: is anything actually LISTENING? The application's
    // wiring had no tests, so deleting AppShell's connect() calls restored the
    // "no way to open a deck" state with the entire suite green.
    int browseListeners() const { return receivers(SIGNAL(browseRequested())); }
    int dropListeners() const { return receivers(SIGNAL(fileDropped(QString))); }
    // The drop HANDLING, callable without Qt's drag-and-drop machinery — a
    // synthetic QDropEvent is not dispatched by QWidget::event, so dropEvent()
    // itself is unreachable from a test. dropEvent is a one-line forward to this,
    // so testing it tests the real path (BUG-60 mutation m23).
    void acceptDroppedMime(const QMimeData* mime);

  signals:
    // The user asked to choose a file. AppShell owns the dialog.
    void browseRequested();
    // The user dropped a file onto the window. Carries the local path only —
    // AppShell decides whether it is loadable.
    void fileDropped(const QString& path);

  protected:
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;

  private:
    QLabel* m_title = nullptr;
    QLabel* m_hint = nullptr;
    QPushButton* m_open = nullptr;
};

} // namespace pptv
