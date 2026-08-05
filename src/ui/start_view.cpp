#include "ui/start_view.hpp"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QKeySequence>
#include <QLabel>
#include <QMimeData>
#include <QPushButton>
#include <QShortcut>
#include <QUrl>
#include <QVBoxLayout>

#include "core/app_info.hpp"

namespace pptv {

QString localDeckPathFrom(const QMimeData* mime) {
    if (!mime || !mime->hasUrls()) {
        return {};
    }
    for (const QUrl& url : mime->urls()) {
        if (url.isLocalFile()) {
            return url.toLocalFile();
        }
    }
    return {};
}

StartView::StartView(QWidget* parent) : QWidget(parent) {
    setWindowTitle(appName());
    setMinimumSize(720, 480);
    setAcceptDrops(true);

    // Minimal-dark theme is the only theme (Project Bible §9). Contrast of the
    // title text on this background is ~13:1, well above the 4.5:1 baseline.
    setStyleSheet(QStringLiteral("background-color: #101114;"));

    m_title = new QLabel(appName(), this);
    m_title->setStyleSheet(QStringLiteral("color: #f2f3f5; font-size: 28px; font-weight: 600;"));
    m_title->setAlignment(Qt::AlignCenter);
    m_title->setAccessibleName(appName());

    m_hint = new QLabel(tr("Open a .pptx to begin, or drop one here"), this);
    m_hint->setStyleSheet(QStringLiteral("color: #9aa0a6; font-size: 15px;"));
    m_hint->setAlignment(Qt::AlignCenter);
    m_hint->setAccessibleName(tr("Deck load status"));

    m_open = new QPushButton(tr("Open deck…"), this);
    m_open->setStyleSheet(QStringLiteral("QPushButton { color: #f2f3f5; background-color: #2b2f36;"
                                         " border: 1px solid #454b54; border-radius: 6px;"
                                         " padding: 10px 22px; font-size: 16px; }"
                                         "QPushButton:hover { background-color: #363b44; }"));
    m_open->setAccessibleName(tr("Open deck"));
    m_open->setDefault(true);
    m_open->setAutoDefault(true);
    connect(m_open, &QPushButton::clicked, this, &StartView::browseRequested);

    // Cmd+O on macOS, Ctrl+O elsewhere — QKeySequence::Open resolves per platform,
    // so the shortcut is right without hard-coding a modifier (the mistake that
    // made the quit chord untypeable in BUG-35/43).
    auto* openShortcut = new QShortcut(QKeySequence::Open, this);
    connect(openShortcut, &QShortcut::activated, this, &StartView::browseRequested);

    auto* layout = new QVBoxLayout(this);
    layout->addStretch();
    layout->addWidget(m_title);
    layout->addWidget(m_hint);
    layout->addSpacing(18);
    layout->addWidget(m_open, 0, Qt::AlignHCenter);
    layout->addStretch();

    m_open->setFocus();
}

void StartView::dragEnterEvent(QDragEnterEvent* e) {
    if (!localDeckPathFrom(e->mimeData()).isEmpty()) {
        e->acceptProposedAction();
    }
}

void StartView::dropEvent(QDropEvent* e) {
    if (localDeckPathFrom(e->mimeData()).isEmpty()) {
        return;
    }
    e->acceptProposedAction();
    acceptDroppedMime(e->mimeData());
}

void StartView::acceptDroppedMime(const QMimeData* mime) {
    const QString path = localDeckPathFrom(mime);
    if (path.isEmpty()) {
        return;
    }
    emit fileDropped(path);
}

} // namespace pptv
