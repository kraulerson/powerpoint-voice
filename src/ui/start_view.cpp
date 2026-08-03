#include "ui/start_view.hpp"

#include <QLabel>
#include <QVBoxLayout>

#include "core/app_info.hpp"

namespace pptv {

StartView::StartView(QWidget* parent) : QWidget(parent) {
    setWindowTitle(appName());
    setMinimumSize(720, 480);

    // Minimal-dark theme is the only theme (Project Bible §9). Contrast of the
    // title text on this background is ~13:1, well above the 4.5:1 baseline.
    setStyleSheet(QStringLiteral("background-color: #101114;"));

    m_title = new QLabel(appName(), this);
    m_title->setStyleSheet(QStringLiteral("color: #f2f3f5; font-size: 28px; font-weight: 600;"));
    m_title->setAlignment(Qt::AlignCenter);
    // Expose a stable accessible name for the Phase 3 accessibility audit.
    m_title->setAccessibleName(appName());

    m_hint = new QLabel(tr("No deck loaded"), this);
    m_hint->setStyleSheet(QStringLiteral("color: #9aa0a6; font-size: 15px;"));
    m_hint->setAlignment(Qt::AlignCenter);
    m_hint->setAccessibleName(tr("Deck load status"));

    auto* layout = new QVBoxLayout(this);
    layout->addStretch();
    layout->addWidget(m_title);
    layout->addWidget(m_hint);
    layout->addStretch();
}

} // namespace pptv
