// render_preview — a headless dev/UAT utility: load a .pptx and render every
// slide to PNG files in an output directory, printing any load warnings. Used to
// eyeball rendering fidelity against a real deck without the (not-yet-built) UI.
//
//   render_preview <deck.pptx> <out-dir> [width height]
//
// The deck is never uploaded anywhere — this runs entirely locally.
#include <cstdio>

#include <QDir>
#include <QGuiApplication>
#include <QImage>

#include "loader/deck_loader.hpp"
#include "render/slide_renderer.hpp"

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    if (argc < 3) {
        std::fprintf(stderr, "usage: render_preview <deck.pptx> <out-dir> [width height]\n");
        return 2;
    }
    const QString deck = QString::fromUtf8(argv[1]);
    const QString outDir = QString::fromUtf8(argv[2]);
    const int w = (argc >= 5) ? QString::fromUtf8(argv[3]).toInt() : 1600;
    const int h = (argc >= 5) ? QString::fromUtf8(argv[4]).toInt() : 900;

    pptv::LoadResult r = pptv::DeckLoader::load(deck);
    if (!r.ok) {
        std::fprintf(stderr, "load failed: %s\n", r.error.message.toUtf8().constData());
        return 1;
    }
    QDir().mkpath(outDir);

    std::printf("Loaded %d slide(s), %d warning(s)\n",
                static_cast<int>(r.presentation.slides.size()),
                static_cast<int>(r.presentation.warnings.size()));
    for (const pptv::LoadWarning& warn : r.presentation.warnings) {
        std::printf("  warning: slide %d: %s (%s)\n", warn.slideIndex,
                    warn.elementType.toUtf8().constData(), warn.detail.toUtf8().constData());
    }

    for (int i = 0; i < static_cast<int>(r.presentation.slides.size()); ++i) {
        const QImage img = pptv::SlideRenderer::render(r.presentation, i, w, h);
        const QString path =
            QStringLiteral("%1/slide-%2.png").arg(outDir).arg(i + 1, 3, 10, QLatin1Char('0'));
        if (!img.save(path)) {
            std::fprintf(stderr, "failed to save %s\n", path.toUtf8().constData());
            return 3;
        }
    }
    std::printf("Rendered %d slide(s) to %s\n", static_cast<int>(r.presentation.slides.size()),
                outDir.toUtf8().constData());
    return 0;
}
