#pragma once

#include <QWidget>

class QLabel;

namespace pptv {

// The dark landing surface shown before a deck is loaded (Project Bible §9,
// UI scaffolding: StartView). The MVP scaffold renders the minimal-dark shell;
// deck selection, the recent-files list, and drag-drop land through the Build
// Loop.
class StartView : public QWidget {
    Q_OBJECT

  public:
    explicit StartView(QWidget* parent = nullptr);

  private:
    QLabel* m_title = nullptr;
    QLabel* m_hint = nullptr;
};

} // namespace pptv
