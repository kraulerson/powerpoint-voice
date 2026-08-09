#pragma once

#include <cstddef>
#include <vector>

#include <QImage>

// A bounded window of pre-rendered slides (BUG-22).
//
// The raster store was an unbounded vector: 3840x2160 RGB32 is ~31.6 MB per slide,
// so a 300-slide deck is ~9.27 GB. That swaps, then the OOM killer takes the app —
// during a talk. The reference deck is 10 slides and was never at risk; this is the
// arbitrary-deck case the Bible ratified a fixed 2 GB window for
// (section 3, A3-1(3) / B1-A).
//
// The policy is deliberately simple and pure so it can be tested without images:
// keep the slides NEAREST the one being shown, because that is what the presenter is
// about to need, and drop the furthest first.
namespace pptv {

inline constexpr std::size_t kRasterBudgetBytes = 2ULL * 1024 * 1024 * 1024;

std::size_t rasterBytes(const QImage& img);

// Indices to evict so that `total` fits `budget`, furthest-from-`current` first.
// Pure: takes the per-slide sizes rather than the images.
std::vector<int> evictionOrder(const std::vector<std::size_t>& sizes, int current,
                               std::size_t budget);

} // namespace pptv
