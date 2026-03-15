#include <gtest/gtest.h>
#include "analysis/pattern_scanner.h"

using namespace orpheus::analysis;

TEST(PatternScanner, CompileSimplePattern) {
    auto pattern = PatternScanner::Compile("48 8B 05 ?? ?? ?? ??", "test");
    ASSERT_TRUE(pattern.has_value());
    EXPECT_EQ(pattern->bytes.size(), 7);
    EXPECT_TRUE(pattern->mask[0]);   // 0x48 - match
    EXPECT_TRUE(pattern->mask[1]);   // 0x8B - match
    EXPECT_TRUE(pattern->mask[2]);   // 0x05 - match
    EXPECT_FALSE(pattern->mask[3]);  // ?? - wildcard
    EXPECT_FALSE(pattern->mask[4]);
    EXPECT_FALSE(pattern->mask[5]);
    EXPECT_FALSE(pattern->mask[6]);
}

TEST(PatternScanner, CompileNoSpaces) {
    auto pattern = PatternScanner::Compile("488B05????????", "test");
    ASSERT_TRUE(pattern.has_value());
    EXPECT_EQ(pattern->bytes.size(), 7);
    EXPECT_EQ(pattern->bytes[0], 0x48);
    EXPECT_EQ(pattern->bytes[1], 0x8B);
    EXPECT_EQ(pattern->bytes[2], 0x05);
    EXPECT_TRUE(pattern->mask[0]);
    EXPECT_FALSE(pattern->mask[3]);
}

TEST(PatternScanner, CompileStarWildcards) {
    auto pattern = PatternScanner::Compile("48 8B ** ** **", "test");
    ASSERT_TRUE(pattern.has_value());
    EXPECT_EQ(pattern->bytes.size(), 5);
    EXPECT_TRUE(pattern->mask[0]);
    EXPECT_TRUE(pattern->mask[1]);
    EXPECT_FALSE(pattern->mask[2]);
    EXPECT_FALSE(pattern->mask[3]);
    EXPECT_FALSE(pattern->mask[4]);
}

TEST(PatternScanner, CompileEmptyReturnsNullopt) {
    auto pattern = PatternScanner::Compile("", "empty");
    EXPECT_FALSE(pattern.has_value());
}

TEST(PatternScanner, CompileInvalidHexReturnsNullopt) {
    auto pattern = PatternScanner::Compile("ZZ GG", "bad");
    EXPECT_FALSE(pattern.has_value());
}

TEST(PatternScanner, CompileOddLengthReturnsNullopt) {
    // After removing spaces, "4 8" -> "48" which is even and valid
    // But "4" alone -> "4" which is odd
    auto pattern = PatternScanner::Compile("4", "odd");
    EXPECT_FALSE(pattern.has_value());
}

TEST(PatternScanner, ScanFindsPattern) {
    // Put enough data to avoid SIMD reading past the buffer
    std::vector<uint8_t> data(32, 0x00);
    data[1] = 0x48;
    data[2] = 0x8B;
    data[3] = 0x05;

    auto pattern = PatternScanner::Compile("48 8B 05", "test");
    ASSERT_TRUE(pattern.has_value());
    auto results = PatternScanner::Scan(data, *pattern, 0x1000, 10);
    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results[0], 0x1001);  // offset 1 + base 0x1000
}

TEST(PatternScanner, ScanWithWildcards) {
    std::vector<uint8_t> data(32, 0x00);
    data[0] = 0x48; data[1] = 0x8B; data[2] = 0x05;
    data[3] = 0x11; data[4] = 0x22; data[5] = 0x33; data[6] = 0x44;

    auto pattern = PatternScanner::Compile("48 8B 05 ?? ?? ?? ??", "test");
    ASSERT_TRUE(pattern.has_value());
    auto results = PatternScanner::Scan(data, *pattern, 0, 10);
    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results[0], 0u);
}

TEST(PatternScanner, ScanNoMatch) {
    std::vector<uint8_t> data(32, 0x00);
    auto pattern = PatternScanner::Compile("FF FF FF", "test");
    ASSERT_TRUE(pattern.has_value());
    auto results = PatternScanner::Scan(data, *pattern, 0, 10);
    EXPECT_TRUE(results.empty());
}

TEST(PatternScanner, ScanMultipleMatches) {
    std::vector<uint8_t> data(64, 0x00);
    // Place pattern at two locations
    data[5] = 0xAA; data[6] = 0xBB;
    data[20] = 0xAA; data[21] = 0xBB;

    auto pattern = PatternScanner::Compile("AA BB", "test");
    ASSERT_TRUE(pattern.has_value());
    auto results = PatternScanner::Scan(data, *pattern, 0, 10);
    ASSERT_EQ(results.size(), 2);
    EXPECT_EQ(results[0], 5u);
    EXPECT_EQ(results[1], 20u);
}

TEST(PatternScanner, ScanMaxResultsLimits) {
    std::vector<uint8_t> data(64, 0x00);
    data[5] = 0xAA; data[6] = 0xBB;
    data[20] = 0xAA; data[21] = 0xBB;

    auto pattern = PatternScanner::Compile("AA BB", "test");
    ASSERT_TRUE(pattern.has_value());
    auto results = PatternScanner::Scan(data, *pattern, 0, 1);  // max 1
    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results[0], 5u);
}

TEST(PatternScanner, FindFirstReturnsFirst) {
    std::vector<uint8_t> data(64, 0x00);
    data[10] = 0xCC; data[11] = 0xDD;
    data[30] = 0xCC; data[31] = 0xDD;

    auto pattern = PatternScanner::Compile("CC DD", "test");
    ASSERT_TRUE(pattern.has_value());
    auto result = PatternScanner::FindFirst(data, *pattern, 0x5000);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 0x500A);  // offset 10 + base
}

TEST(PatternScanner, FindFirstNoMatch) {
    std::vector<uint8_t> data(32, 0x00);
    auto pattern = PatternScanner::Compile("FF EE DD", "test");
    ASSERT_TRUE(pattern.has_value());
    auto result = PatternScanner::FindFirst(data, *pattern, 0);
    EXPECT_FALSE(result.has_value());
}

TEST(PatternScanner, QuickScanWorks) {
    std::vector<uint8_t> data(32, 0x00);
    data[0] = 0xE8; data[1] = 0x01; data[2] = 0x02; data[3] = 0x03; data[4] = 0x04;

    auto results = PatternScanner::QuickScan(data, "E8 ?? ?? ?? ??", 0);
    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results[0], 0u);
}

TEST(PatternScanner, QuickScanInvalidPattern) {
    std::vector<uint8_t> data(32, 0x00);
    auto results = PatternScanner::QuickScan(data, "", 0);
    EXPECT_TRUE(results.empty());
}

TEST(PatternScanner, ScanEmptyData) {
    std::vector<uint8_t> data;
    auto pattern = PatternScanner::Compile("48 8B", "test");
    ASSERT_TRUE(pattern.has_value());
    auto results = PatternScanner::Scan(data, *pattern, 0, 10);
    EXPECT_TRUE(results.empty());
}

TEST(PatternScanner, PatternLargerThanData) {
    std::vector<uint8_t> data = {0x48};
    auto pattern = PatternScanner::Compile("48 8B 05", "test");
    ASSERT_TRUE(pattern.has_value());
    auto results = PatternScanner::Scan(data, *pattern, 0, 10);
    EXPECT_TRUE(results.empty());
}

TEST(PatternScanner, ScanMultiplePatterns) {
    std::vector<uint8_t> data(64, 0x00);
    data[0] = 0xE8; // CALL
    data[10] = 0xE9; // JMP

    auto call_pat = PatternScanner::Compile("E8", "call");
    auto jmp_pat = PatternScanner::Compile("E9", "jmp");
    ASSERT_TRUE(call_pat.has_value());
    ASSERT_TRUE(jmp_pat.has_value());

    std::vector<Pattern> patterns = {*call_pat, *jmp_pat};
    auto results = PatternScanner::ScanMultiple(data, patterns, 0x1000, 4);
    ASSERT_GE(results.size(), 2u);

    // Should find both patterns
    bool found_call = false, found_jmp = false;
    for (const auto& match : results) {
        if (match.address == 0x1000) found_call = true;
        if (match.address == 0x100A) found_jmp = true;
    }
    EXPECT_TRUE(found_call);
    EXPECT_TRUE(found_jmp);
}
