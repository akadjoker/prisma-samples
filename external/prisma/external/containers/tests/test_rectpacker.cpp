#define CT_RECTPACKER_IMPLEMENTATION
#include <ct/rectpacker.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <ctime>
#include <random>
#include <vector>

namespace
{
    using Packer = ct::RectPacker;

    bool is_power_of_two(int32_t value)
    {
        return value > 0 && (value & (value - 1)) == 0;
    }

    bool overlaps(int32_t ax, int32_t ay, int32_t aw, int32_t ah, int32_t bx, int32_t by,
                  int32_t bw, int32_t bh)
    {
        return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
    }

    void expect_valid_page(const Packer::Page &page, int32_t padding)
    {
        EXPECT_TRUE(is_power_of_two(page.atlas_width));
        EXPECT_TRUE(is_power_of_two(page.atlas_height));
        for (size_t i = 0; i < page.placed.size(); ++i)
        {
            const Packer::PlacedRect &rect = page.placed[i];
            EXPECT_GE(rect.x, 0);
            EXPECT_GE(rect.y, 0);
            EXPECT_LE(rect.x + rect.width + padding, page.atlas_width);
            EXPECT_LE(rect.y + rect.height + padding, page.atlas_height);
            for (size_t j = i + 1; j < page.placed.size(); ++j)
            {
                const Packer::PlacedRect &other = page.placed[j];
                EXPECT_FALSE(overlaps(rect.x, rect.y, rect.width + padding,
                                      rect.height + padding, other.x, other.y,
                                      other.width + padding, other.height + padding));
            }
        }
    }
}

TEST(RectPacker, EmptyInputProducesNoPages)
{
    EXPECT_TRUE(Packer::pack_pages({}, 64, 64).empty());
}

TEST(RectPacker, PacksSingleRectIntoSmallestPowerOfTwoPage)
{
    const ct::Vector<Packer::Page> pages = Packer::pack_pages({{7, 13, 7}}, 128, 128, 1);
    ASSERT_EQ(pages.size(), 1u);
    const Packer::Page &page = pages[0];
    ASSERT_EQ(page.placed.size(), 1u);
    EXPECT_EQ(page.atlas_width, 16);
    EXPECT_EQ(page.atlas_height, 8);
    EXPECT_EQ(page.placed[0].id, 7);
    EXPECT_EQ(page.placed[0].x, 0);
    EXPECT_EQ(page.placed[0].y, 0);
    EXPECT_EQ(page.placed[0].width, 13);
    EXPECT_EQ(page.placed[0].height, 7);
    expect_valid_page(page, 1);
}

TEST(RectPacker, RespectsPaddingAndNeverOverlaps)
{
    const ct::Vector<Packer::Page> pages = Packer::pack_pages(
        {{1, 17, 11}, {2, 13, 19}, {3, 9, 7}, {4, 5, 21}, {5, 8, 8}, {6, 4, 4}},
        64, 64, 2);

    ASSERT_EQ(pages.size(), 1u);
    ASSERT_EQ(pages[0].placed.size(), 6u);
    expect_valid_page(pages[0], 2);
}

TEST(RectPacker, SplitsItemsAcrossPages)
{
    const ct::Vector<Packer::Page> pages =
        Packer::pack_pages({{1, 16, 15}, {2, 16, 15}, {3, 16, 15}}, 32, 32, 1);

    ASSERT_EQ(pages.size(), 2u);
    EXPECT_EQ(pages[0].placed.size(), 2u);
    EXPECT_EQ(pages[1].placed.size(), 1u);
    for (const Packer::Page &page : pages)
        expect_valid_page(page, 1);
}

TEST(RectPacker, UsesPowerOfTwoCapsAndDropsImpossibleRects)
{
    const ct::Vector<Packer::Page> pages = Packer::pack_pages(
        {{1, 32, 32}, {2, 31, 31}, {3, 0, 5}, {4, -1, 3}}, 63, 63, 0);

    ASSERT_EQ(pages.size(), 2u);
    EXPECT_EQ(pages[0].atlas_width, 32);
    EXPECT_EQ(pages[0].atlas_height, 32);
    EXPECT_EQ(pages[0].placed.size() + pages[1].placed.size(), 2u);
    for (const Packer::Page &page : pages)
        expect_valid_page(page, 0);
}

TEST(RectPacker, InvalidPageLimitsProduceNoPages)
{
    EXPECT_TRUE(Packer::pack_pages({{1, 1, 1}}, 0, 64).empty());
    EXPECT_TRUE(Packer::pack_pages({{1, 1, 1}}, 64, -1).empty());
}

TEST(RectPacker, FuzzNuncaSobrepoeEColocaCadaIdUmaVez)
{
    std::mt19937 rng(7);
    for (int round = 0; round < 25; ++round)
    {
        const int count = 1 + static_cast<int>(rng() % 300);
        const int32_t cap = 1 << (5 + rng() % 6);
        const int32_t padding = static_cast<int32_t>(rng() % 3);
        ct::Vector<Packer::Input> inputs;
        for (int i = 0; i < count; ++i)
            inputs.push_back({i, static_cast<int32_t>(1 + rng() % (cap / 2)), static_cast<int32_t>(1 + rng() % (cap / 2))});
        const ct::Vector<Packer::Page> pages = Packer::pack_pages(inputs, cap, cap, padding);
        std::vector<int> seen(static_cast<std::size_t>(count), 0);
        for (const Packer::Page &page : pages)
        {
            expect_valid_page(page, padding);
            EXPECT_LE(page.atlas_width, cap);
            EXPECT_LE(page.atlas_height, cap);
            for (const Packer::PlacedRect &rect : page.placed)
            {
                ASSERT_GE(rect.id, 0);
                ASSERT_LT(rect.id, count);
                ++seen[static_cast<std::size_t>(rect.id)];
                EXPECT_EQ(rect.width, inputs[static_cast<std::size_t>(rect.id)].width);
                EXPECT_EQ(rect.height, inputs[static_cast<std::size_t>(rect.id)].height);
            }
        }
        for (int i = 0; i < count; ++i)
        {
            const bool fits = inputs[static_cast<std::size_t>(i)].width + padding <= cap && inputs[static_cast<std::size_t>(i)].height + padding <= cap;
            EXPECT_EQ(seen[static_cast<std::size_t>(i)], fits ? 1 : 0) << "id " << i;
        }
    }
}

TEST(RectPacker, MilharesDeSpritesEmpacotamEmTempoRazoavel)
{
    std::mt19937 rng(1);
    ct::Vector<Packer::Input> inputs;
    for (int i = 0; i < 3000; ++i)
        inputs.push_back({i, static_cast<int32_t>(8 + rng() % 120), static_cast<int32_t>(8 + rng() % 120)});
    const clock_t start = std::clock();
    const ct::Vector<Packer::Page> pages = Packer::pack_pages(inputs, 4096, 4096, 1);
    const double seconds = double(std::clock() - start) / CLOCKS_PER_SEC;
    ASSERT_FALSE(pages.empty());
    std::size_t placed = 0;
    for (const Packer::Page &page : pages)
        placed += page.placed.size();
    EXPECT_EQ(placed, 3000u);
    EXPECT_LT(seconds, 10.0) << "3000 sprites a 4096 demoraram " << seconds << " s";
}
