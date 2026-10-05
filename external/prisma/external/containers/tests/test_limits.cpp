#include <ct/arena.hpp>
#include <ct/deque.hpp>
#include <ct/hashmap.hpp>
#include <ct/pool.hpp>
#include <ct/string.hpp>
#include <ct/vector.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <limits>

TEST(Limits, VectorReservePastMaximumAborts)
{
    ct::Vector<int> values;
    EXPECT_DEATH(values.reserve(values.max_size() + 1), "");
}

TEST(Limits, DequeReservePastMaximumAborts)
{
    ct::Deque<int> values;
    EXPECT_DEATH(values.reserve(values.max_size() + 1), "");
}

TEST(Limits, StringReservePastMaximumAborts)
{
    ct::String value;
    EXPECT_DEATH(value.reserve(value.max_size() + 1), "");
}

TEST(Limits, HashMapReserveOverflowAborts)
{
    ct::HashMap<int, int> values;
    EXPECT_DEATH(values.reserve((std::numeric_limits<std::size_t>::max)()), "");
}

TEST(Limits, ArenaArrayOverflowAborts)
{
    ct::Arena arena;
    EXPECT_DEATH(arena.allocate_array<std::uint64_t>(
        (std::numeric_limits<std::size_t>::max)() / sizeof(std::uint64_t) + 1), "");
}

TEST(Limits, PoolSlotCountOverflowAborts)
{
    const std::size_t slots = (std::numeric_limits<std::size_t>::max)();
    EXPECT_DEATH({ ct::Pool<int> pool{slots}; (void)pool; }, "");
}
