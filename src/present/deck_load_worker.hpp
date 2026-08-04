#pragma once

#include <atomic>
#include <functional>
#include <memory>

#include <QByteArray>
#include <QObject>
#include <QString>

#include "loader/deck_loader.hpp"

// Off-thread deck loading (Feature F7b).
//
// Parsing an untrusted .pptx is unbounded work (TM-014/015/018): a large or hostile
// deck can take many seconds. It must therefore never run on the UI thread, or the
// app appears frozen while the presenter is standing in front of the room.
namespace pptv {

// The parsed deck, carried by shared_ptr so the queued hand-off to the GUI thread
// moves a pointer rather than deep-copying an entire deck (which for a 300-slide
// deck with images is hundreds of MB).
using PresentationPtr = std::shared_ptr<const Presentation>;

struct DeckLoadOutcome {
    bool ok = false;
    LoadError error;
    PresentationPtr presentation;
    long long fileBytes = 0; // measured; survives the thread boundary
    QByteArray deckSha;      // short content hash, for the session log (never the path)
};

// Registers the metatypes needed to carry these across a queued connection. Must be
// called once before the worker is used; returns non-zero ids.
int registerPresentMetaTypes();

// Maps a load failure to a FIXED, user-facing string. Deliberately ignores
// LoadError::message, which embeds the deck's full path and (for a hostile archive)
// attacker-controlled bytes — neither may reach a dialog that can land on the
// projector (Project Bible section 8, TM-013).
QString describeLoadError(LoadErrorKind kind);

// A short, stable content hash. Used to identify a deck in the session log WITHOUT
// recording its filename or any of its content (Project Bible section 8).
QString sha256Short(const QByteArray& bytes);

class DeckLoadWorker : public QObject {
    Q_OBJECT

  public:
    // The load function is injectable so tests can drive every failure path and the
    // cancel path without a real file on disk.
    using LoadFn = std::function<LoadResult(const QString&)>;

    explicit DeckLoadWorker(QObject* parent = nullptr);

    void setLoadFn(LoadFn fn) { loadFn_ = std::move(fn); }
    void setPath(const QString& path) { path_ = path; }

  public slots:
    void start();
    // Callable from any thread. A result that arrives after cancel is discarded —
    // the presenter has moved on, and delivering it would surprise them.
    void cancel();

  signals:
    void loaded(DeckLoadOutcome outcome);
    void finished();

  private:
    LoadFn loadFn_;
    QString path_;
    std::atomic<bool> cancelled_{false};
};

} // namespace pptv

Q_DECLARE_METATYPE(pptv::DeckLoadOutcome)
