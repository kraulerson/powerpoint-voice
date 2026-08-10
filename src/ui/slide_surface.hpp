#pragma once

#include <QImage>
#include <QWidget>

#include "present/display_geometry.hpp"

namespace pptv {

// Paints one slide raster, aspect-preserved and centred on black (Feature F7b).
// It holds NO policy: where to draw comes from the pure fitRect(), and what to draw
// is handed to it. A widget with no decisions in it cannot make a wrong one.
class SlideSurface : public QWidget {
    Q_OBJECT

  public:
    explicit SlideSurface(QWidget* parent = nullptr);

    void setSlideImage(const QImage& img);
    void setStatusText(const QString& text); // shown when there is no raster yet
    QRectF lastPaintedRect() const { return lastRect_; }

    // What a screen reader says this surface currently IS (A11Y-1).
    //
    // A custom-painted QWidget is, to VoiceOver, an unnamed rectangle: the whole
    // presentation was one silent black box, so a presenter using a screen reader
    // could not tell which slide was showing, whether the projector was blanked, or
    // whether a slide was still rendering. Nothing on this surface is text, so there
    // is nothing for the platform to infer — it has to be stated.
    //
    // Deliberately NEVER the deck's own content: it says "Slide 3 of 10", not what is
    // on slide 3. Slide text is Confidential (Bible section 8, TM-012/013) and the
    // accessibility tree is readable by other processes.
    void setAccessibleState(const QString& what);

  protected:
    void paintEvent(QPaintEvent* e) override;

  private:
    QImage image_;
    QString status_;
    QRectF lastRect_;
};

} // namespace pptv
