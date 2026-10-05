#pragma once

#include "detail/utils.hpp"
#include "span.hpp"
#include "vector.hpp"

namespace ct
{

    template <typename T>
    struct Handle32
    {
        enum : std::uint32_t
        {
            kIndexBits = 20,
            kGenerationBits = 32 - kIndexBits,
            kIndexMask = (1u << kIndexBits) - 1u,
            kGenerationMask = (1u << kGenerationBits) - 1u
        };

        std::uint32_t value;

        constexpr Handle32() noexcept : value(0) {}
        constexpr Handle32(std::uint32_t i, std::uint32_t g) noexcept
            : value(((g & kGenerationMask) << kIndexBits) | (i & kIndexMask)) {}

        constexpr std::uint32_t index() const noexcept { return value & kIndexMask; }
        constexpr std::uint32_t generation() const noexcept { return value >> kIndexBits; }

        constexpr bool valid() const noexcept { return generation() != 0; }
        constexpr explicit operator bool() const noexcept { return generation() != 0; }

        constexpr std::uint32_t bits() const noexcept { return value; }
        static constexpr Handle32 from_bits(std::uint32_t b) noexcept
        {
            return Handle32(b & kIndexMask, b >> kIndexBits);
        }

        std::uint64_t hash() const noexcept { return detail::hash_mix(value); }

        bool operator==(const Handle32 &o) const noexcept { return value == o.value; }
        bool operator!=(const Handle32 &o) const noexcept { return value != o.value; }
    };

    template <typename T, typename Alloc = HeapAlloc>
    class SlotMap32
    {
    public:
        using value_type = T;
        using size_type = std::size_t;
        using handle_type = Handle32<T>;
        using iterator = T *;
        using const_iterator = const T *;

        SlotMap32() noexcept : free_head_(kNoFree) {}

        size_type size() const noexcept { return dense_.size(); }
        bool empty() const noexcept { return dense_.empty(); }
        size_type capacity() const noexcept { return dense_.capacity(); }
        size_type slot_count() const noexcept { return slots_.size(); }
        static size_type max_slots() noexcept { return kMaxSlots; }

        void reserve(size_type n)
        {
            dense_.reserve(n);
            owners_.reserve(n);
            slots_.reserve(n);
        }

        handle_type insert(const T &v)
        {
            const std::uint32_t slot = acquire_slot();
            dense_.push_back(v);
            return finish_insert(slot);
        }

        handle_type insert(T &&v)
        {
            const std::uint32_t slot = acquire_slot();
            dense_.push_back(detail::move(v));
            return finish_insert(slot);
        }

        template <typename... Args>
        handle_type emplace(Args &&...args)
        {
            const std::uint32_t slot = acquire_slot();
            dense_.emplace_back(detail::forward<Args>(args)...);
            return finish_insert(slot);
        }

        bool erase(handle_type h)
        {
            if (!contains(h))
                return false;
            const std::uint32_t index = h.index();
            Slot &s = slots_[index];
            const std::uint32_t hole = s.dense;
            const std::uint32_t last = static_cast<std::uint32_t>(dense_.size() - 1);
            if (hole != last)
            {
                dense_[hole] = detail::move(dense_[last]);
                owners_[hole] = owners_[last];
                slots_[owners_[hole]].dense = hole;
            }
            dense_.pop_back();
            owners_.pop_back();

            ++s.generation;
            s.dense = free_head_;
            free_head_ = index;
            return true;
        }

        void clear()
        {
            dense_.clear();
            owners_.clear();
            free_head_ = kNoFree;
            for (size_type i = slots_.size(); i > 0; --i)
            {
                Slot &s = slots_[i - 1];
                if (s.generation & 1u)
                    ++s.generation;
                s.dense = free_head_;
                free_head_ = static_cast<std::uint32_t>(i - 1);
            }
        }

        bool contains(handle_type h) const noexcept
        {
            const std::uint32_t generation = h.generation();
            const std::uint32_t index = h.index();
            return (generation & 1u) != 0 && index < slots_.size() &&
                   (slots_[index].generation & kGenerationMask) == generation;
        }

        T *get(handle_type h) noexcept
        {
            return contains(h) ? &dense_[slots_[h.index()].dense] : nullptr;
        }

        const T *get(handle_type h) const noexcept
        {
            return contains(h) ? &dense_[slots_[h.index()].dense] : nullptr;
        }

        T &operator[](handle_type h)
        {
            if (CT_UNLIKELY(!contains(h)))
                detail::fatal("ct::SlotMap32: handle invalido (objeto ja apagado?)");
            return dense_[slots_[h.index()].dense];
        }

        const T &operator[](handle_type h) const
        {
            if (CT_UNLIKELY(!contains(h)))
                detail::fatal("ct::SlotMap32: handle invalido (objeto ja apagado?)");
            return dense_[slots_[h.index()].dense];
        }

        Span<T> items() noexcept { return Span<T>(dense_.data(), dense_.size()); }
        Span<const T> items() const noexcept
        {
            return Span<const T>(dense_.data(), dense_.size());
        }

        iterator begin() noexcept { return dense_.data(); }
        iterator end() noexcept { return dense_.end(); }
        const_iterator begin() const noexcept { return dense_.data(); }
        const_iterator end() const noexcept { return dense_.end(); }

        handle_type handle_at(size_type i) const
        {
            if (CT_UNLIKELY(i >= dense_.size()))
                detail::fatal("ct::SlotMap32::handle_at: index fora dos limites");
            const std::uint32_t slot = owners_[i];
            return handle_type(slot, slots_[slot].generation);
        }

        size_type index_of(handle_type h) const
        {
            if (CT_UNLIKELY(!contains(h)))
                detail::fatal("ct::SlotMap32::index_of: handle invalido");
            return slots_[h.index()].dense;
        }

    private:
        struct Slot
        {
            std::uint32_t dense;
            std::uint32_t generation;
        };

        static const std::uint32_t kNoFree = 0xFFFFFFFFu;
        static const std::uint32_t kMaxSlots = 1u << handle_type::kIndexBits;
        static const std::uint32_t kGenerationMask = handle_type::kGenerationMask;

        std::uint32_t acquire_slot()
        {
            if (free_head_ != kNoFree)
            {
                const std::uint32_t i = free_head_;
                free_head_ = slots_[i].dense;
                ++slots_[i].generation;
                return i;
            }
            if (CT_UNLIKELY(slots_.size() >= kMaxSlots))
                detail::fatal("ct::SlotMap32: slots esgotados (2^20)");
            Slot s;
            s.dense = 0;
            s.generation = 1;
            slots_.push_back(s);
            return static_cast<std::uint32_t>(slots_.size() - 1);
        }

        handle_type finish_insert(std::uint32_t slot)
        {
            owners_.push_back(slot);
            slots_[slot].dense = static_cast<std::uint32_t>(dense_.size() - 1);
            return handle_type(slot, slots_[slot].generation);
        }

        Vector<T, Alloc> dense_;
        Vector<std::uint32_t, Alloc> owners_;
        Vector<Slot, Alloc> slots_;
        std::uint32_t free_head_;
    };

}
