#include <doctest/doctest.h>

#include <cstdlib>
#include <vector>

#include "present/raster_cache.hpp"

using namespace pptv;

// ===========================================================================
// GROUP RC — BUG-22: the raster window is bounded.
//
// Unbounded, a 300-slide 4K deck is ~9.27 GB of QImage: the machine swaps, then the
// OOM killer takes the app, during a talk. The reference deck is 10 slides and was
// never at risk; this is the arbitrary-deck case.
// ===========================================================================

TEST_CASE("RC: the budget is the ratified 2 GB") {
    CHECK(kRasterBudgetBytes == 2ULL * 1024 * 1024 * 1024);
}

TEST_CASE("RC: under budget, nothing is evicted") {
    CHECK(evictionOrder({100, 100, 100}, 1, 1000).empty());
    CHECK(evictionOrder({}, 0, 1000).empty());
}

TEST_CASE("RC: eviction takes the FURTHEST slides first") {
    // 10 slides of 100 bytes, budget 500: five must go, and they must be the ones
    // furthest from what the presenter is looking at.
    const std::vector<std::size_t> sizes(10, 100);
    const auto evict = evictionOrder(sizes, 5, 500);
    REQUIRE(evict.size() == 5);
    // Slide 0 is furthest from 5, so it goes first.
    CHECK(evict.front() == 0);
    // Nothing adjacent to the current slide is evicted while distant slides remain.
    for (int i : evict) {
        CHECK(std::abs(i - 5) >= 2);
    }
}

TEST_CASE("RC: the slide being SHOWN is never evicted") {
    // Evicting it blanks the projector — the one outcome this subsystem exists to
    // prevent. Budget of 0 forces maximum pressure.
    const std::vector<std::size_t> sizes(8, 1000);
    for (int cur = 0; cur < 8; ++cur) {
        const auto evict = evictionOrder(sizes, cur, 0);
        for (int i : evict) {
            CHECK(i != cur);
        }
        CHECK(evict.size() == 7); // everything else can go
    }
}

TEST_CASE("RC: eviction stops as soon as the budget is met, not later") {
    const std::vector<std::size_t> sizes(10, 100); // 1000 total
    CHECK(evictionOrder(sizes, 5, 900).size() == 1);
    CHECK(evictionOrder(sizes, 5, 400).size() == 6);
}

TEST_CASE("RC: already-empty slots are not counted as evictable work") {
    std::vector<std::size_t> sizes(10, 0);
    sizes[3] = 5000;
    sizes[4] = 5000;
    const auto evict = evictionOrder(sizes, 4, 5000);
    REQUIRE(evict.size() == 1);
    CHECK(evict[0] == 3); // the only non-empty, non-current slot
}

TEST_CASE("RC: a real 4K deck is bounded — the failure this exists to prevent") {
    // 3840x2160 RGB32 ~ 33.2 MB per slide. 300 slides is ~9.7 GB unbounded.
    const std::size_t perSlide = 3840ULL * 2160ULL * 4ULL;
    const std::vector<std::size_t> sizes(300, perSlide);
    const auto evict = evictionOrder(sizes, 150, kRasterBudgetBytes);
    CHECK_FALSE(evict.empty());
    std::size_t remaining = 0;
    std::vector<bool> gone(300, false);
    for (int i : evict) {
        gone[static_cast<std::size_t>(i)] = true;
    }
    for (std::size_t i = 0; i < sizes.size(); ++i) {
        if (!gone[i]) {
            remaining += sizes[i];
        }
    }
    CHECK(remaining <= kRasterBudgetBytes);
    CHECK_FALSE(gone[150]); // still showing the current slide
}
