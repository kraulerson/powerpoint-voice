#pragma once

#include <QString>

// The presentation layer's user-facing message vocabulary (Feature F7).
//
// The vocabulary is CLOSED by construction: a notice is an id plus at most two
// integers, never free text. That is what makes it impossible for deck content,
// a file path, or (later) heard speech to reach the audience display or a log —
// there is no channel for it (Project Bible §8, threats TM-012/TM-013).
namespace pptv {

enum class NoticeId {
    None,
    CommandEcho,        // moved: arg = new slide, arg2 = previous slide
    DeckHasSlides,      // out-of-range request rejected: arg = deck length
    AlreadyOnSlide,     // requested the slide already shown: arg = slide
    EndOfDeck,          // at the last slide: arg = slide, arg2 = deck length
    AtFirstSlide,       // at the first slide
    DeckEmpty,          // no deck loaded
    Paused,             // a voice command was suppressed because we are paused
    Resumed,            // the keyboard resumed a paused session
    SlideNumberTooLong, // typed slide-number buffer hit its cap
    HoldingHint,        // the holding screen's instruction line
};

// How prominent/persistent a notice is. Transient notices fade; Sticky ones stay
// until the condition clears.
enum class NoticeClass { Transient, Sticky };

// Which surface a notice is allowed to appear on. The audience screen shows only
// what is safe for an audience to see.
enum class NoticeRole { Audience, Operator };

struct Notice {
    NoticeId id = NoticeId::None;
    int arg = 0;
    int arg2 = 0;
    NoticeClass cls = NoticeClass::Transient;

    bool isNone() const { return id == NoticeId::None; }
};

// Renders a notice for a surface. Returns an empty string when the notice must
// not be shown on that surface. Never includes deck content — the inputs are
// integers only.
QString noticeForRole(const Notice& n, NoticeRole role, bool paused);

} // namespace pptv
