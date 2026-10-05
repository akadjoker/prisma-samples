#include "Check.h"
#include "prisma/rhi/vulkan/FreeRanges.h"

#include <stdio.h>

namespace
{

struct Piece
{
    std::uint32_t block;
    std::uint64_t offset;
    std::uint64_t size;
    bool live;
};

std::uint32_t seed = 12345;

std::uint32_t random(std::uint32_t limit)
{
    seed = seed * 1664525u + 1013904223u;
    return (seed >> 8) % limit;
}

} // namespace

int main()
{
    using namespace prisma;

    FreeRanges ranges;
    std::uint64_t offset = 99;
    CHECK(!ranges.take(1, 16, 1, &offset));

    ranges.add(1, 1024);
    CHECK(ranges.whole(1, 1024));
    CHECK(ranges.take(1, 100, 1, &offset));
    CHECK(offset == 0);
    CHECK(ranges.take(1, 100, 64, &offset));
    CHECK(offset == 128);
    CHECK(ranges.freeBytes(1) == 1024 - 200);
    CHECK(!ranges.take(1, 2000, 1, &offset));
    CHECK(!ranges.take(2, 16, 1, &offset));
    ranges.give(1, 0, 100);
    ranges.give(1, 128, 100);
    CHECK(ranges.whole(1, 1024));
    CHECK(ranges.count() == 1);

    const std::uint64_t kBlock = 4096;
    ranges.add(2, kBlock);
    static Piece pieces[256];
    int count = 0;
    bool overlap = false;
    bool misaligned = false;
    for (int step = 0; step < 20000; ++step)
    {
        if (count < 256 && random(3) != 0)
        {
            Piece piece;
            piece.block = 1 + random(2);
            piece.size = 1 + random(200);
            piece.live = true;
            const std::uint64_t alignment = 1u << random(7);
            if (!ranges.take(piece.block, piece.size, alignment, &piece.offset)) continue;
            if (piece.offset % alignment != 0) misaligned = true;
            const std::uint64_t limit = piece.block == 1 ? 1024 : kBlock;
            if (piece.offset + piece.size > limit) overlap = true;
            for (int i = 0; i < count; ++i)
                if (pieces[i].block == piece.block &&
                        pieces[i].offset < piece.offset + piece.size &&
                        piece.offset < pieces[i].offset + pieces[i].size)
                    overlap = true;
            pieces[count++] = piece;
        }
        else if (count > 0)
        {
            const int index = static_cast<int>(random(static_cast<std::uint32_t>(count)));
            ranges.give(pieces[index].block, pieces[index].offset, pieces[index].size);
            pieces[index] = pieces[--count];
        }
        std::uint64_t used[2] = { 0, 0 };
        for (int i = 0; i < count; ++i) used[pieces[i].block - 1] += pieces[i].size;
        if (ranges.freeBytes(1) + used[0] != 1024 || ranges.freeBytes(2) + used[1] != kBlock)
            overlap = true;
    }
    CHECK(!overlap);
    CHECK(!misaligned);
    for (int i = 0; i < count; ++i) ranges.give(pieces[i].block, pieces[i].offset, pieces[i].size);
    CHECK(ranges.whole(1, 1024));
    CHECK(ranges.whole(2, kBlock));
    CHECK(ranges.count() == 2);

    ranges.remove(1);
    CHECK(!ranges.whole(1, 1024));
    CHECK(ranges.count() == 1);

    if (failures)
    {
        printf("%d failed\n", failures);
        return 1;
    }
    printf("ok\n");
    return 0;
}
