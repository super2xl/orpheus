#include <gtest/gtest.h>
#include "utils/bookmarks.h"
#include "utils/logger.h"

using namespace orpheus;

// Initialize logger once before bookmark tests run (LOG_INFO/LOG_ERROR crash without it)
class BookmarkTestEnvironment : public ::testing::Environment {
public:
    void SetUp() override {
        Logger::Instance().Initialize();
    }
};

static auto* const env = ::testing::AddGlobalTestEnvironment(new BookmarkTestEnvironment);

TEST(BookmarkManager, AddAndRetrieve) {
    BookmarkManager mgr;
    size_t idx = mgr.Add(0x1000, "test_label", "some notes", "general", "client.dll");

    EXPECT_EQ(idx, 0u);
    auto all = mgr.GetAll();
    ASSERT_EQ(all.size(), 1u);
    EXPECT_EQ(all[0].address, 0x1000u);
    EXPECT_EQ(all[0].label, "test_label");
    EXPECT_EQ(all[0].notes, "some notes");
    EXPECT_EQ(all[0].category, "general");
    EXPECT_EQ(all[0].module, "client.dll");
    EXPECT_NE(all[0].created_at, 0);  // Should be auto-set
}

TEST(BookmarkManager, AddMultiple) {
    BookmarkManager mgr;
    mgr.Add(0x1000, "a", "", "", "");
    mgr.Add(0x2000, "b", "", "", "");
    mgr.Add(0x3000, "c", "", "", "");

    EXPECT_EQ(mgr.Count(), 3u);
}

TEST(BookmarkManager, RemoveByIndex) {
    BookmarkManager mgr;
    mgr.Add(0x1000, "a", "", "", "");
    mgr.Add(0x2000, "b", "", "", "");

    EXPECT_TRUE(mgr.Remove(0));
    EXPECT_EQ(mgr.Count(), 1u);
    EXPECT_EQ(mgr.GetAll()[0].address, 0x2000u);
}

TEST(BookmarkManager, RemoveByIndexOutOfRange) {
    BookmarkManager mgr;
    mgr.Add(0x1000, "a", "", "", "");
    EXPECT_FALSE(mgr.Remove(5));
    EXPECT_EQ(mgr.Count(), 1u);
}

TEST(BookmarkManager, RemoveByAddress) {
    BookmarkManager mgr;
    mgr.Add(0x1000, "a", "", "", "");
    mgr.Add(0x2000, "b", "", "", "");

    EXPECT_TRUE(mgr.RemoveByAddress(0x1000));
    auto all = mgr.GetAll();
    ASSERT_EQ(all.size(), 1u);
    EXPECT_EQ(all[0].address, 0x2000u);
}

TEST(BookmarkManager, RemoveByAddressNotFound) {
    BookmarkManager mgr;
    mgr.Add(0x1000, "a", "", "", "");
    EXPECT_FALSE(mgr.RemoveByAddress(0x9999));
    EXPECT_EQ(mgr.Count(), 1u);
}

TEST(BookmarkManager, IsBookmarked) {
    BookmarkManager mgr;
    mgr.Add(0x1000, "test", "", "", "");

    EXPECT_TRUE(mgr.IsBookmarked(0x1000));
    EXPECT_FALSE(mgr.IsBookmarked(0x2000));
}

TEST(BookmarkManager, FindByAddress) {
    BookmarkManager mgr;
    mgr.Add(0x1000, "found_me", "", "", "");

    const Bookmark* bm = mgr.FindByAddress(0x1000);
    ASSERT_NE(bm, nullptr);
    EXPECT_EQ(bm->label, "found_me");

    EXPECT_EQ(mgr.FindByAddress(0x9999), nullptr);
}

TEST(BookmarkManager, GetByCategory) {
    BookmarkManager mgr;
    mgr.Add(0x1000, "a", "", "functions", "");
    mgr.Add(0x2000, "b", "", "data", "");
    mgr.Add(0x3000, "c", "", "functions", "");

    auto funcs = mgr.GetByCategory("functions");
    EXPECT_EQ(funcs.size(), 2u);

    auto data = mgr.GetByCategory("data");
    EXPECT_EQ(data.size(), 1u);

    auto empty = mgr.GetByCategory("nonexistent");
    EXPECT_TRUE(empty.empty());
}

TEST(BookmarkManager, GetCategories) {
    BookmarkManager mgr;
    mgr.Add(0x1000, "a", "", "functions", "");
    mgr.Add(0x2000, "b", "", "data", "");
    mgr.Add(0x3000, "c", "", "functions", "");

    auto cats = mgr.GetCategories();
    EXPECT_EQ(cats.size(), 2u);
    // Categories should be sorted (std::set)
    EXPECT_EQ(cats[0], "data");
    EXPECT_EQ(cats[1], "functions");
}

TEST(BookmarkManager, EmptyCategoryNotReturned) {
    BookmarkManager mgr;
    mgr.Add(0x1000, "a", "", "", "");  // empty category

    auto cats = mgr.GetCategories();
    EXPECT_TRUE(cats.empty());
}

TEST(BookmarkManager, Update) {
    BookmarkManager mgr;
    mgr.Add(0x1000, "original", "", "", "");

    Bookmark updated;
    updated.address = 0x2000;
    updated.label = "updated";
    EXPECT_TRUE(mgr.Update(0, updated));

    auto all = mgr.GetAll();
    EXPECT_EQ(all[0].address, 0x2000u);
    EXPECT_EQ(all[0].label, "updated");
}

TEST(BookmarkManager, UpdateOutOfRange) {
    BookmarkManager mgr;
    mgr.Add(0x1000, "a", "", "", "");

    Bookmark bm;
    EXPECT_FALSE(mgr.Update(5, bm));
}

TEST(BookmarkManager, Clear) {
    BookmarkManager mgr;
    mgr.Add(0x1000, "a", "", "", "");
    mgr.Add(0x2000, "b", "", "", "");

    mgr.Clear();
    EXPECT_EQ(mgr.Count(), 0u);
    EXPECT_TRUE(mgr.GetAll().empty());
}

TEST(BookmarkManager, DirtyFlag) {
    BookmarkManager mgr;
    EXPECT_FALSE(mgr.IsDirty());

    mgr.Add(0x1000, "a", "", "", "");
    EXPECT_TRUE(mgr.IsDirty());

    mgr.ClearDirty();
    EXPECT_FALSE(mgr.IsDirty());

    mgr.RemoveByAddress(0x1000);
    EXPECT_TRUE(mgr.IsDirty());
}

TEST(BookmarkManager, AddViaStruct) {
    BookmarkManager mgr;
    Bookmark bm;
    bm.address = 0xDEAD;
    bm.label = "struct_add";
    bm.notes = "test notes";
    bm.category = "cat1";
    bm.module = "mod.dll";

    size_t idx = mgr.Add(bm);
    EXPECT_EQ(idx, 0u);

    auto all = mgr.GetAll();
    ASSERT_EQ(all.size(), 1u);
    EXPECT_EQ(all[0].address, 0xDEADu);
    EXPECT_EQ(all[0].label, "struct_add");
}
