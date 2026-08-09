#include "present/raster_cache.hpp"

#include <algorithm>
#include <cstdlib>

namespace pptv {

std::size_t rasterBytes(const QImage& img) {
    if (img.isNull()) {
        return 0;
    }
    // sizeInBytes() is the allocation Qt actually made, including any row padding —
    // width*height*depth would under-count and let the window drift over budget.
    return static_cast<std::size_t>(std::max<qsizetype>(0, img.sizeInBytes()));
}

std::vector<int> evictionOrder(const std::vector<std::size_t>& sizes, int current,
                               std::size_t budget) {
    std::size_t total = 0;
    for (std::size_t b : sizes) {
        total += b;
    }
    if (total <= budget) {
        return {};
    }
    // Furthest from the current slide first. The slide being shown is never a
    // candidate: evicting it would blank the projector, which is the one outcome
    // this whole subsystem exists to prevent.
    std::vector<int> candidates;
    for (int i = 0; i < static_cast<int>(sizes.size()); ++i) {
        if (i != current && sizes[static_cast<std::size_t>(i)] > 0) {
            candidates.push_back(i);
        }
    }
    std::sort(candidates.begin(), candidates.end(), [current](int a, int b) {
        const int da = std::abs(a - current);
        const int db = std::abs(b - current);
        return da != db ? da > db : a > b; // deterministic on ties
    });

    std::vector<int> evict;
    for (int i : candidates) {
        if (total <= budget) {
            break;
        }
        total -= sizes[static_cast<std::size_t>(i)];
        evict.push_back(i);
    }
    return evict;
}

} // namespace pptv
