#include <gtest/gtest.h>
#include "core/memory_cache.h"
#include <thread>

using namespace orpheus;

TEST(MemoryCache, StoreAndRetrieve) {
    MemoryCache cache;
    cache.SetEnabled(true);
    cache.SetTTL(5000);  // long TTL for test

    std::vector<uint8_t> data = {0x48, 0x8B, 0x05};
    cache.Put(1, 0x1000, data);

    auto result = cache.Get(1, 0x1000, 3);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->size(), 3u);
    EXPECT_EQ((*result)[0], 0x48);
    EXPECT_EQ((*result)[1], 0x8B);
    EXPECT_EQ((*result)[2], 0x05);
}

TEST(MemoryCache, MissOnDifferentPid) {
    MemoryCache cache;
    cache.SetEnabled(true);
    cache.SetTTL(5000);

    std::vector<uint8_t> data = {0x01, 0x02};
    cache.Put(1, 0x1000, data);

    auto result = cache.Get(2, 0x1000, 2);  // different PID
    EXPECT_FALSE(result.has_value());
}

TEST(MemoryCache, MissOnDifferentAddress) {
    MemoryCache cache;
    cache.SetEnabled(true);
    cache.SetTTL(5000);

    std::vector<uint8_t> data = {0x01, 0x02};
    cache.Put(1, 0x1000, data);

    // Address on a different page
    auto result = cache.Get(1, 0x2000, 2);
    EXPECT_FALSE(result.has_value());
}

TEST(MemoryCache, DisabledCacheReturnsMiss) {
    MemoryCache cache;
    cache.SetEnabled(false);

    std::vector<uint8_t> data = {0x01};
    cache.Put(1, 0x1000, data);

    auto result = cache.Get(1, 0x1000, 1);
    EXPECT_FALSE(result.has_value());
}

TEST(MemoryCache, EnableDisableToggle) {
    MemoryCache cache;
    cache.SetEnabled(true);
    cache.SetTTL(5000);

    cache.Put(1, 0x1000, {0x01});
    EXPECT_TRUE(cache.Get(1, 0x1000, 1).has_value());

    // Disabling clears the cache
    cache.SetEnabled(false);
    EXPECT_FALSE(cache.Get(1, 0x1000, 1).has_value());

    // Re-enable - data is gone
    cache.SetEnabled(true);
    EXPECT_FALSE(cache.Get(1, 0x1000, 1).has_value());
}

TEST(MemoryCache, ClearRemovesAll) {
    MemoryCache cache;
    cache.SetEnabled(true);
    cache.SetTTL(5000);

    cache.Put(1, 0x1000, {0x01});
    cache.Put(1, 0x2000, {0x02});
    cache.Clear();

    EXPECT_FALSE(cache.Get(1, 0x1000, 1).has_value());
    EXPECT_FALSE(cache.Get(1, 0x2000, 1).has_value());
}

TEST(MemoryCache, Stats) {
    MemoryCache cache;
    cache.SetEnabled(true);
    cache.SetTTL(5000);

    cache.Put(1, 0x1000, {0x01, 0x02, 0x03, 0x04});
    cache.Get(1, 0x1000, 4);  // hit
    cache.Get(1, 0x2000, 4);  // miss

    auto stats = cache.GetStats();
    EXPECT_EQ(stats.hits, 1u);
    EXPECT_EQ(stats.misses, 1u);
}

TEST(MemoryCache, ResetStats) {
    MemoryCache cache;
    cache.SetEnabled(true);
    cache.SetTTL(5000);

    cache.Put(1, 0x1000, {0x01});
    cache.Get(1, 0x1000, 1);  // hit
    cache.Get(1, 0x2000, 1);  // miss

    cache.ResetStats();
    auto stats = cache.GetStats();
    EXPECT_EQ(stats.hits, 0u);
    EXPECT_EQ(stats.misses, 0u);
}

TEST(MemoryCache, HitRateCalculation) {
    MemoryCache cache;
    cache.SetEnabled(true);
    cache.SetTTL(5000);

    cache.Put(1, 0x1000, {0x01});
    cache.Get(1, 0x1000, 1);  // hit
    cache.Get(1, 0x2000, 1);  // miss

    auto stats = cache.GetStats();
    EXPECT_DOUBLE_EQ(stats.HitRate(), 0.5);
}

TEST(MemoryCache, HitRateWithNoAccesses) {
    MemoryCache cache;
    auto stats = cache.GetStats();
    EXPECT_DOUBLE_EQ(stats.HitRate(), 0.0);
}

TEST(MemoryCache, InvalidateAddress) {
    MemoryCache cache;
    cache.SetEnabled(true);
    cache.SetTTL(5000);

    cache.Put(1, 0x1000, {0x01});
    cache.Put(1, 0x2000, {0x02});

    cache.Invalidate(1, 0x1000, 1);

    EXPECT_FALSE(cache.Get(1, 0x1000, 1).has_value());
    EXPECT_TRUE(cache.Get(1, 0x2000, 1).has_value());
}

TEST(MemoryCache, InvalidateProcess) {
    MemoryCache cache;
    cache.SetEnabled(true);
    cache.SetTTL(5000);

    cache.Put(1, 0x1000, {0x01});
    cache.Put(1, 0x2000, {0x02});
    cache.Put(2, 0x1000, {0x03});

    cache.InvalidateProcess(1);

    EXPECT_FALSE(cache.Get(1, 0x1000, 1).has_value());
    EXPECT_FALSE(cache.Get(1, 0x2000, 1).has_value());
    EXPECT_TRUE(cache.Get(2, 0x1000, 1).has_value());
}

TEST(MemoryCache, TTLExpiry) {
    MemoryCache cache;
    cache.SetEnabled(true);
    cache.SetTTL(1);  // 1ms TTL

    cache.Put(1, 0x1000, {0x01});

    // Sleep to let TTL expire
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    auto result = cache.Get(1, 0x1000, 1);
    EXPECT_FALSE(result.has_value());
}

TEST(MemoryCache, MaxPagesEviction) {
    MemoryCache cache;
    cache.SetEnabled(true);
    cache.SetTTL(5000);
    cache.SetMaxPages(2);  // Only 2 pages

    // EvictIfNeeded uses > (not >=), so eviction triggers when adding
    // a page that would exceed max_pages. Insert 4 pages to guarantee eviction.
    cache.Put(1, 0x0000, {0x01});
    cache.Put(1, 0x1000, {0x02});
    cache.Put(1, 0x2000, {0x03});
    cache.Put(1, 0x3000, {0x04});

    auto stats = cache.GetStats();
    EXPECT_LE(stats.current_pages, 3u);
    EXPECT_GE(stats.evictions, 1u);
}

TEST(MemoryCache, GetZeroSizeReturnsMiss) {
    MemoryCache cache;
    cache.SetEnabled(true);
    cache.SetTTL(5000);

    cache.Put(1, 0x1000, {0x01});
    auto result = cache.Get(1, 0x1000, 0);
    EXPECT_FALSE(result.has_value());
}

TEST(MemoryCache, PageAlignment) {
    MemoryCache cache;
    cache.SetEnabled(true);
    cache.SetTTL(5000);

    // Put data at a non-page-aligned address
    // Address 0x1010 is on page 0x1000
    std::vector<uint8_t> data = {0xAA, 0xBB, 0xCC, 0xDD};
    cache.Put(1, 0x1010, data);

    // Read back from the same non-aligned address
    auto result = cache.Get(1, 0x1010, 4);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ((*result)[0], 0xAA);
    EXPECT_EQ((*result)[1], 0xBB);
}

TEST(MemoryCache, ConfigRoundtrip) {
    MemoryCache cache;

    MemoryCache::Config cfg;
    cfg.max_pages = 512;
    cfg.ttl_ms = 200;
    cfg.enabled = true;
    cache.SetConfig(cfg);

    auto got = cache.GetConfig();
    EXPECT_EQ(got.max_pages, 512u);
    EXPECT_EQ(got.ttl_ms, 200u);
    EXPECT_TRUE(got.enabled);
}
