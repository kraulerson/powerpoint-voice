#include "render/slide_renderer.hpp"

#include <algorithm>

#include <QBuffer>
#include <QColor>
#include <QFont>
#include <QImageReader>
#include <QList>
#include <QPainter>
#include <QPointF>
#include <QRectF>
#include <QTextCharFormat>
#include <QTextLayout>
#include <QTextOption>

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
// True only for the two formats we allow, decided from the file's own magic bytes.
// This runs BEFORE any Qt decoder is constructed, which matters for two reasons:
//  1. SAFETY — attacker-controlled bytes never reach a codec we have not allow-listed.
//  2. STABILITY — QImageReader::format() PROBES the installed image plugins to
//     identify an unknown format. That plugin load happens on whatever thread calls
//     it, and the pre-render worker is not the GUI thread; on macOS this crashed the
//     app outright on a deck containing EMF parts (Karl's real deck, UAT-3 human run).
bool isAllowedImageFormat(const QByteArray& d) {
    static const QByteArray kPng("\x89PNG\r\n\x1a\n", 8);
    if (d.size() >= 8 && d.left(8) == kPng) {
        return true;
    }
    // JPEG: SOI marker FF D8 FF
    return d.size() >= 3 && static_cast<unsigned char>(d[0]) == 0xFF &&
           static_cast<unsigned char>(d[1]) == 0xD8 && static_cast<unsigned char>(d[2]) == 0xFF;
}

QImage decodeGuarded(const QByteArray& data) {
    if (data.isEmpty()) {
        return {};
    }
    if (!isAllowedImageFormat(data)) {
        return {}; // EMF/WMF/TIFF/WebP/GIF/... -> placeholder, no decoder touched
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

// Draws one text box via QTextLayout: real per-run color/font (BUG-5),
// word-wrap within the box (BUG-4), and U+2028 line breaks (BUG-6), with bullet
// prefixes + list indent (BUG-7). Text with no color uses `defaultTextColor`
// (chosen from the background luminance, BUG-1). A box with no geometry falls
// back to a default content area of the slide (BUG-2 safety net).
void drawTextBox(QPainter& p, const TextBox& tb, double scale, double offX, double offY,
                 double maxFontPx, const QColor& defaultTextColor, const QRectF& slideRect) {
    QRectF box;
    if (tb.rect.cx > 0 && tb.rect.cy > 0) {
        box = QRectF(offX + tb.rect.x * scale, offY + tb.rect.y * scale, tb.rect.cx * scale,
                     tb.rect.cy * scale);
    } else {
        box = slideRect.adjusted(slideRect.width() * 0.08, slideRect.height() * 0.10,
                                 -slideRect.width() * 0.08, -slideRect.height() * 0.10);
    }

    const double indentUnit = box.width() * 0.04;
    double cursorY = box.top();

    for (const Paragraph& para : tb.paragraphs) {
        if (para.runs.empty()) {
            continue;
        }
        const double indent = para.indentLevel * indentUnit;

        auto fontFor = [&](const TextRun& r) {
            QFont f;
            if (!r.fontFamily.isEmpty()) {
                f.setFamily(r.fontFamily);
            }
            f.setPixelSize(clampFontPx(r.fontSizePt * kEmuPerPoint * scale, maxFontPx));
            f.setBold(r.bold);
            f.setItalic(r.italic);
            return f;
        };
        auto colorFor = [&](const TextRun& r) {
            return r.color ? toQColor(*r.color) : defaultTextColor;
        };

        QString text;
        QList<QTextLayout::FormatRange> formats;
        auto addSpan = [&](const QString& s, const QFont& f, const QColor& col) {
            if (s.isEmpty()) {
                return;
            }
            QTextLayout::FormatRange fr;
            fr.start = static_cast<int>(text.size());
            fr.length = static_cast<int>(s.size());
            QTextCharFormat fmt;
            fmt.setFont(f);
            fmt.setForeground(col);
            fr.format = fmt;
            formats.append(fr);
            text += s;
        };

        const TextRun& lead = para.runs.front();
        if (!para.bulletChar.isEmpty()) {
            addSpan(para.bulletChar + QLatin1Char(' '), fontFor(lead), colorFor(lead));
        }
        for (const TextRun& r : para.runs) {
            addSpan(r.text, fontFor(r), colorFor(r));
        }
        if (text.isEmpty()) {
            continue;
        }

        QTextLayout layout(text);
        QTextOption opt;
        opt.setWrapMode(QTextOption::WordWrap);
        layout.setTextOption(opt);
        layout.setFormats(formats);
        layout.beginLayout();
        for (;;) {
            QTextLine line = layout.createLine();
            if (!line.isValid()) {
                break;
            }
            line.setLineWidth(box.width() - indent);
            line.setPosition(QPointF(box.left() + indent, cursorY));
            cursorY += line.height();
        }
        layout.endLayout();
        layout.draw(&p, QPointF(0, 0));
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

// The sub-rectangle of a source image that <a:srcRect> selects. DrawingML gives the
// inset from each edge in thousandths of a percent, so l="29178" discards the leftmost
// 29.178%. Insets may be NEGATIVE, which asks for padding outside the image; there is
// nothing to sample there, so those are clamped to the image bounds rather than
// producing a Qt-undefined out-of-bounds source rect.
QRectF sourceRect(const QSize& imageSize, const SrcRect& sr) {
    const QRectF whole(0, 0, imageSize.width(), imageSize.height());
    if (sr.isIdentity() || imageSize.isEmpty()) {
        return whole;
    }
    constexpr double kFull = 100000.0;
    // Insets that meet or cross leave nothing to show. Computing it anyway yields a
    // NEGATIVE width, and QRectF::intersected NORMALISES that — it swaps the edges
    // and returns a valid rectangle covering exactly the region the deck excluded,
    // drawn mirrored (adversarial review F2). The width guard downstream never fires
    // because the normalised width is positive. Reject here instead.
    // Sum in 64-bit. These are ints straight out of the deck, so two large values
    // overflow signed int here — UNDEFINED BEHAVIOUR in the guard whose entire job
    // is to reject bad input (adversarial review F5, reproduced under UBSan against
    // the library). A guard that invokes UB on the inputs it exists to catch is
    // worse than no guard.
    const qint64 hSum = static_cast<qint64>(sr.leftPerMille) + sr.rightPerMille;
    const qint64 vSum = static_cast<qint64>(sr.topPerMille) + sr.bottomPerMille;
    if (hSum >= 100000 || vSum >= 100000) {
        return {};
    }
    const double x = imageSize.width() * (sr.leftPerMille / kFull);
    const double y = imageSize.height() * (sr.topPerMille / kFull);
    const double w = imageSize.width() * (1.0 - (sr.leftPerMille + sr.rightPerMille) / kFull);
    const double h = imageSize.height() * (1.0 - (sr.topPerMille + sr.bottomPerMille) / kFull);
    return QRectF(x, y, w, h).intersected(whole);
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

    // Background, and the default text color chosen from its luminance so text
    // with no explicit/resolvable color stays readable (BUG-1): white on dark,
    // black on light.
    QColor bgColor = Qt::white; // None/Picture -> white default
    if (slide.background.kind == BackgroundKind::Solid && slide.background.solid) {
        bgColor = toQColor(*slide.background.solid);
    }
    p.fillRect(slideRect, bgColor);
    const double luma =
        (0.299 * bgColor.red() + 0.587 * bgColor.green() + 0.114 * bgColor.blue()) / 255.0;
    const QColor defaultTextColor = (luma < 0.5) ? QColor(Qt::white) : QColor(Qt::black);

    // Elements in z-order.
    for (const ShapeElement& e : slide.elements) {
        switch (e.kind) {
        case ElementKind::TextBox:
            drawTextBox(p, e.textBox, scale, offX, offY, maxFontPx, defaultTextColor, slideRect);
            break;
        case ElementKind::Image: {
            const QRectF r = pxRect(e.image.rect, scale, offX, offY);
            const QImage decoded = decodeGuarded(e.image.imageData);
            if (decoded.isNull()) {
                drawPlaceholderBox(p, r, QStringLiteral("missing image"), maxFontPx);
            } else if (r.width() >= 1 && r.height() >= 1) {
                // Honour <a:srcRect> FIRST (BUG-37): the deck may show only part of
                // the source, and everything below must reason about that part. On
                // the real deck a picture cropped 29.178% off each side was drawn
                // whole, so the white margins PowerPoint discards appeared as a box
                // around the artwork on a dark slide.
                QRectF src = sourceRect(decoded.size(), e.image.srcRect);
                // Only a TRULY empty source is nothing to draw. The guard used to be
                // `< 1`, measured in SOURCE pixels — so an ordinary 600x2 accent bar
                // cropped to 0.8 source pixels high vanished with no warning and no
                // placeholder (adversarial review F3). That is the same silent
                // picture loss BUG-41 exists to fix, reintroduced one commit later.
                // A sub-pixel source is still real content: round it outward to one
                // pixel, clamped to the image, and draw it.
                if (src.width() <= 0.0 || src.height() <= 0.0) {
                    break;
                }
                if (src.width() < 1.0) {
                    src.setX(qMin(src.x(), static_cast<double>(decoded.width()) - 1.0));
                    src.setWidth(1.0);
                }
                if (src.height() < 1.0) {
                    src.setY(qMin(src.y(), static_cast<double>(decoded.height()) - 1.0));
                    src.setHeight(1.0);
                }
                // Preserve aspect ratio (BUG-10): fit within the frame and center,
                // so a frame whose aspect differs from the image does not squish it.
                const QSizeF fit = QSizeF(src.size()).scaled(r.size(), Qt::KeepAspectRatio);
                const QRectF dst(r.x() + (r.width() - fit.width()) / 2.0,
                                 r.y() + (r.height() - fit.height()) / 2.0, fit.width(),
                                 fit.height());
                // <a:alphaModFix> — uniform picture opacity. Restored immediately so
                // one translucent picture cannot wash out everything drawn after it.
                const double alpha = e.image.alphaPerMille / 100000.0;
                const bool translucent = alpha < 0.999;
                if (translucent) {
                    p.setOpacity(alpha);
                }
                p.drawImage(dst, decoded, src);
                if (translucent) {
                    p.setOpacity(1.0);
                }
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
