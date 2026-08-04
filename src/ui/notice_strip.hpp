#pragma once

#include <QString>
#include <QWidget>

namespace pptv {

// The thin message band along the bottom of the presentation window (Feature F7b).
// Its height is BOUNDED and its text ELIDES: a long message must never grow the
// strip and push the slide off the projector.
class NoticeStrip : public QWidget {
    Q_OBJECT

  public:
    explicit NoticeStrip(QWidget* parent = nullptr);

    void setText(const QString& text);
    QString text() const { return text_; }

    // Bounded: min(10% of the host height, kMaxHeight), never more.
    static int heightFor(int hostHeight);
    static constexpr int kMaxHeight = 72;
    static constexpr int kMinHeight = 24;

  protected:
    void paintEvent(QPaintEvent* e) override;

  private:
    QString text_;
};

} // namespace pptv
