#pragma once

#include <ct/vector.hpp>

#include <cstddef>
#include <cstdint>

namespace prisma
{

class FreeRanges
{
public:
    void add(std::uint32_t block, std::uint64_t size)
    {
        const Range range = { block, 0, size };
        ranges_.push_back(range);
    }

    void remove(std::uint32_t block)
    {
        std::size_t kept = 0;
        for (std::size_t i = 0; i < ranges_.size(); ++i)
            if (ranges_[i].block != block) ranges_[kept++] = ranges_[i];
        ranges_.resize(kept);
    }

    bool take(std::uint32_t block, std::uint64_t size, std::uint64_t alignment,
            std::uint64_t* offset)
    {
        if (alignment == 0) alignment = 1;
        std::size_t best = ranges_.size();
        bool exact = false;
        for (std::size_t i = 0; i < ranges_.size() && !exact; ++i)
        {
            const Range& range = ranges_[i];
            if (range.block != block) continue;
            const std::uint64_t aligned = alignUp(range.offset, alignment);
            const std::uint64_t padding = aligned - range.offset;
            if (size + padding > range.size) continue;
            best = i;
            exact = padding == 0;
        }
        if (best == ranges_.size()) return false;

        const Range range = ranges_[best];
        const std::uint64_t aligned = alignUp(range.offset, alignment);
        const std::uint64_t padding = aligned - range.offset;
        const std::uint64_t tail = range.offset + range.size - (aligned + size);
        if (padding > 0) ranges_[best].size = padding;
        if (tail > 0)
        {
            const Range rest = { block, aligned + size, tail };
            if (padding > 0) ranges_.push_back(rest);
            else
                ranges_[best] = rest;
        }
        if (padding == 0 && tail == 0) removeAt(best);
        *offset = aligned;
        return true;
    }

    void give(std::uint32_t block, std::uint64_t offset, std::uint64_t size)
    {
        const Range given = { block, offset, size };
        ranges_.push_back(given);
        std::size_t current = ranges_.size() - 1;
        bool merged = true;
        while (merged)
        {
            merged = false;
            for (std::size_t i = 0; i < ranges_.size() && !merged; ++i)
            {
                if (i == current || ranges_[i].block != block) continue;
                Range& mine = ranges_[current];
                const Range other = ranges_[i];
                if (other.offset + other.size == mine.offset) mine.offset = other.offset;
                else if (mine.offset + mine.size != other.offset)
                    continue;
                mine.size += other.size;
                if (current == ranges_.size() - 1) current = i;
                removeAt(i);
                merged = true;
            }
        }
    }

    bool whole(std::uint32_t block, std::uint64_t size) const
    {
        for (std::size_t i = 0; i < ranges_.size(); ++i)
            if (ranges_[i].block == block && ranges_[i].offset == 0 && ranges_[i].size == size)
                return true;
        return false;
    }

    std::uint64_t freeBytes(std::uint32_t block) const
    {
        std::uint64_t total = 0;
        for (std::size_t i = 0; i < ranges_.size(); ++i)
            if (ranges_[i].block == block) total += ranges_[i].size;
        return total;
    }

    std::size_t count() const { return ranges_.size(); }

private:
    struct Range
    {
        std::uint32_t block;
        std::uint64_t offset;
        std::uint64_t size;
    };

    static std::uint64_t alignUp(std::uint64_t value, std::uint64_t alignment)
    {
        return (value + alignment - 1) / alignment * alignment;
    }

    void removeAt(std::size_t index)
    {
        ranges_[index] = ranges_[ranges_.size() - 1];
        ranges_.pop_back();
    }

    ct::Vector<Range> ranges_;
};

} // namespace prisma
