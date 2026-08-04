#include "present/deck_load_worker.hpp"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QMetaType>

namespace pptv {

int registerPresentMetaTypes() {
    // Required before a DeckLoadOutcome can cross a queued connection; without it
    // the signal is silently dropped at runtime, which is a very confusing failure.
    return qRegisterMetaType<DeckLoadOutcome>("pptv::DeckLoadOutcome");
}

QString describeLoadError(LoadErrorKind kind) {
    switch (kind) {
    case LoadErrorKind::None:
        return QStringLiteral("The deck could not be opened.");
    case LoadErrorKind::FileNotFound:
        return QStringLiteral("That file could not be found.");
    case LoadErrorKind::NotAZip:
        return QStringLiteral("That file is not a PowerPoint (.pptx) file.");
    case LoadErrorKind::MissingPresentationPart:
        return QStringLiteral("That file is not a valid PowerPoint deck.");
    case LoadErrorKind::MalformedXml:
        return QStringLiteral("That deck is damaged and could not be read.");
    case LoadErrorKind::FileTooLarge:
        return QStringLiteral("That deck is too large to open safely.");
    case LoadErrorKind::TooManySlides:
        return QStringLiteral("That deck has too many slides to open safely.");
    case LoadErrorKind::PartTooLarge:
    case LoadErrorKind::DecompressionLimit:
        return QStringLiteral("That deck expands to too much data to open safely.");
    }
    return QStringLiteral("The deck could not be opened.");
}

QString sha256Short(const QByteArray& bytes) {
    // Identifies a deck in the session log by CONTENT, so the log never has to hold
    // the filename (which is itself Confidential — Project Bible section 8, TM-013).
    const QByteArray full = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
    return QString::fromLatin1(full.toHex().left(8));
}

DeckLoadWorker::DeckLoadWorker(QObject* parent) : QObject(parent) {}

void DeckLoadWorker::cancel() {
    // Callable from any thread: a plain atomic flag, checked at the few points where
    // the worker can safely stop. No locks, so a cancel can never deadlock the UI.
    cancelled_.store(true, std::memory_order_relaxed);
}

void DeckLoadWorker::start() {
    // Cancelled before we even began — do no work at all.
    if (cancelled_.load(std::memory_order_relaxed)) {
        emit finished();
        return;
    }

    DeckLoadOutcome out;
    if (loadFn_) {
        // Measure the file before parsing: the size is reported even on the failure
        // paths (e.g. FileTooLarge), where the parse never produces a deck.
        const QFileInfo info(path_);
        out.fileBytes = info.exists() ? info.size() : 0;
        // Hash only a file we would actually accept. Reading first meant a 20 GB
        // file was pulled entirely into memory BEFORE DeckLoader applied
        // maxFileBytes (audit M5).
        if (info.exists() && out.fileBytes > 0 && out.fileBytes <= LoaderLimits{}.maxFileBytes) {
            QFile f(path_);
            if (f.open(QIODevice::ReadOnly)) {
                out.deckSha = sha256Short(f.readAll()).toLatin1();
            }
        }

        const LoadResult r = loadFn_(path_);
        out.ok = r.ok;
        out.error = r.error;
        if (r.ok) {
            // Move the parsed deck into a shared_ptr so the hand-off to the GUI
            // thread copies a pointer, not hundreds of MB of slides and images.
            out.presentation = std::make_shared<const Presentation>(std::move(r.presentation));
        }
    }

    // Re-check AFTER the parse: a deck that finishes arriving once the presenter has
    // already moved on must be dropped, not delivered late over whatever they are
    // now looking at.
    if (cancelled_.load(std::memory_order_relaxed)) {
        emit finished();
        return;
    }

    emit loaded(out);
    emit finished();
}

} // namespace pptv
