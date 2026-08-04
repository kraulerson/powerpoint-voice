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

  protected:
    void paintEvent(QPaintEvent* e) override;

  private:
    QImage image_;
    QString status_;
    QRectF lastRect_;
};

} // namespace pptv
