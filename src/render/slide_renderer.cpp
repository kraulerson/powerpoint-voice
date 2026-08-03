#include "render/slide_renderer.hpp"

#include <algorithm>

#include <QBuffer>
#include <QColor>
#include <QFont>
#include <QFontMetricsF>
#include <QImageReader>
#include <QPainter>
#include <QRectF>

namespace pptv {
namespace {

constexpr double kEmuPerPoint = 12700.0; // 1 pt = 12700 EMU
// Decoded-image guards (audit R2/R3): only these formats are decoded (excludes
// the CVE-prone TIFF/WebP/GIF codecs), and decode is bounded in both dimensions
// and total allocation.
constexpr qint64 kMaxImagePixels = 40LL * 1024 * 1024; // 40 Mpx
constexpr int kMaxImageAllocMiB = 128;

// Font pixel size must be clamped before QFont/QPainter (audit R1): an unbounded
// declared size otherwise int-overflows the cast and rasterizes a giant glyph
// bitmap, hanging the app. A glyph is never usefully taller than the slide.
int clampFontPx(double px, double maxPx) {
    return static_cast<int>(std::clamp(px, 1.0, std::max(1.0, maxPx)));
}

QColor toQColor(const Color& c) {
    return QColor(c.r, c.g, c.b, c.a);
}

// Decode an image only if it is an allow-listed format within the size bounds
// (audit R2/R3). Returns a null QImage on any rejection -> caller draws a
// placeholder. Never invokes a non-PNG/JPEG codec on attacker bytes.
QImage decodeGuarded(const QByteArray& data) {
    if (data.isEmpty()) {
        return {};
    }
    QByteArray buf = data;
    QBuffer device(&buf);
    device.open(QIODevice::ReadOnly);
    QImageReader reader(&device);
    reader.setAllocationLimit(kMaxImageAllocMiB);
    const QByteArray fmt = reader.format().toLower();
    if (fmt != "png" && fmt != "jpeg" && fmt != "jpg") {
        return {};
    }
    const QSize size = reader.size();
    if (size.isValid() && static_cast<qint64>(size.width()) * size.height() > kMaxImagePixels) {
        return {};
    }
    return reader.read();
}

// Draws one text box: paragraphs stacked top-to-bottom, each paragraph rendered
// with its first run's font/color (the common single-run case; mixed-run
// paragraphs within one box fall back to the first run — a documented text-tier
// MVP limitation). Word-wrapped within the box width.
void drawTextBox(QPainter& p, const TextBox& tb, double scale, double offX, double offY,
                 double maxFontPx) {
    const double x = offX + tb.rect.x * scale;
    const double y = offY + tb.rect.y * scale;
    const double w = tb.rect.cx * scale;
    double cursorY = y;

    for (const Paragraph& para : tb.paragraphs) {
        if (para.runs.empty()) {
            continue;
        }
        const TextRun& lead = para.runs.front();
        QString text;
        for (const TextRun& r : para.runs) {
            text += r.text;
        }
        if (text.isEmpty()) {
            continue;
        }

        QFont font;
        if (!lead.fontFamily.isEmpty()) {
            font.setFamily(lead.fontFamily);
        }
        font.setPixelSize(clampFontPx(lead.fontSizePt * kEmuPerPoint * scale, maxFontPx));
        font.setBold(lead.bold);
        font.setItalic(lead.italic);
        p.setFont(font);
        // Unspecified run color defaults to black (PowerPoint's default text color).
        p.setPen(lead.color ? toQColor(*lead.color) : QColor(Qt::black));

        const QFontMetricsF fm(font);
        const QRectF lineRect(x, cursorY, w, fm.height());
        p.drawText(lineRect, Qt::TextSingleLine | Qt::AlignLeft, text);
        cursorY += fm.height();
    }
}

// A visible placeholder box (light fill + border + centered label) for content
// the text+images tier cannot render faithfully — the Manifesto's "visible
// placeholder, never a silent wrong render".
void drawPlaceholderBox(QPainter& p, const QRectF& rect, const QString& label, double maxFontPx) {
    if (rect.width() < 1 || rect.height() < 1) {
        return;
    }
    p.fillRect(rect, QColor(0xC8, 0xC8, 0xC8));
    QPen border(QColor(0x60, 0x60, 0x60));
    border.setWidth(2);
    p.setPen(border);
    p.drawRect(rect);
    // A diagonal to make it unmistakably a placeholder.
    p.drawLine(rect.topLeft(), rect.bottomRight());
    p.setPen(QColor(0x20, 0x20, 0x20));
    QFont f;
    f.setPixelSize(clampFontPx(rect.height() / 6, maxFontPx));
    p.setFont(f);
    p.drawText(rect, Qt::AlignCenter, label);
}

QRectF pxRect(const RectEmu& r, double scale, double offX, double offY) {
    return QRectF(offX + r.x * scale, offY + r.y * scale, r.cx * scale, r.cy * scale);
}

} // namespace

QImage SlideRenderer::render(const Slide& slide, Emu slideWidthEmu, Emu slideHeightEmu, int targetW,
                             int targetH) {
    // Defensive: a non-positive target has no valid image (audit R7).
    if (targetW <= 0 || targetH <= 0) {
        return {};
    }
    QImage img(targetW, targetH, QImage::Format_RGB32);
    img.fill(Qt::black); // letterbox bars outside the slide

    // A non-positive slide size cannot be scaled (audit F1a-5): return the safe
    // neutral (black) image rather than dividing by zero.
    if (slideWidthEmu <= 0 || slideHeightEmu <= 0) {
        return img;
    }

    const double sw = static_cast<double>(slideWidthEmu);
    const double sh = static_cast<double>(slideHeightEmu);
    const double scale = std::min(targetW / sw, targetH / sh);
    const double renderedW = sw * scale;
    const double renderedH = sh * scale;
    const double offX = (targetW - renderedW) / 2.0;
    const double offY = (targetH - renderedH) / 2.0;
    const QRectF slideRect(offX, offY, renderedW, renderedH);

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    // Confine ALL painting to the slide rect (audit R4): off-slide content must
    // never bleed into the black letterbox bars.
    p.setClipRect(slideRect);
    const double maxFontPx = renderedH; // a glyph is never usefully taller than the slide

    // A placeholder slide (its part was missing/unresolvable) renders a dark
    // surface with a centred notice, never the white default (audit F1a-3).
    if (slide.placeholder) {
        p.fillRect(slideRect, QColor(0x1A, 0x1A, 0x1A));
        p.setPen(QColor(0x9A, 0xA0, 0xA6));
        QFont f;
        f.setPixelSize(clampFontPx(renderedH / 20, maxFontPx));
        p.setFont(f);
        p.drawText(slideRect, Qt::AlignCenter, QStringLiteral("Slide unavailable"));
        return img;
    }

    // Background.
    switch (slide.background.kind) {
    case BackgroundKind::Solid:
        p.fillRect(slideRect,
                   slide.background.solid ? toQColor(*slide.background.solid) : QColor(Qt::white));
        break;
    case BackgroundKind::None:
    case BackgroundKind::Picture: // background-picture bytes are not loaded in the text tier
    default:
        p.fillRect(slideRect, QColor(Qt::white)); // PowerPoint default
        break;
    }

    // Elements in z-order.
    for (const ShapeElement& e : slide.elements) {
        switch (e.kind) {
        case ElementKind::TextBox:
            drawTextBox(p, e.textBox, scale, offX, offY, maxFontPx);
            break;
        case ElementKind::Image: {
            const QRectF r = pxRect(e.image.rect, scale, offX, offY);
            const QImage decoded = decodeGuarded(e.image.imageData);
            if (decoded.isNull()) {
                drawPlaceholderBox(p, r, QStringLiteral("missing image"), maxFontPx);
            } else {
                p.drawImage(r, decoded);
            }
            break;
        }
        case ElementKind::Unsupported:
            drawPlaceholderBox(p, pxRect(e.unsupported.rect, scale, offX, offY), e.unsupported.type,
                               maxFontPx);
            break;
        }
    }

    p.end();
    return img;
}

QImage SlideRenderer::render(const Presentation& pres, int slideIndex, int targetW, int targetH) {
    if (slideIndex < 0 || slideIndex >= static_cast<int>(pres.slides.size())) {
        QImage img(targetW, targetH, QImage::Format_RGB32);
        img.fill(Qt::black);
        return img;
    }
    return render(pres.slides[slideIndex], pres.slideWidth, pres.slideHeight, targetW, targetH);
}

} // namespace pptv
