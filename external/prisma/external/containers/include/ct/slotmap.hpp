#pragma once

#include "detail/utils.hpp"
#include "span.hpp"
#include "vector.hpp"

namespace ct
{

    template <typename T>
    struct Handle
    {
        std::uint32_t index;
        std::uint32_t generation; 

        constexpr Handle() noexcept : index(0), generation(0) {}
        constexpr Handle(std::uint32_t i, std::uint32_t g) noexcept : index(i), generation(g) {}

        constexpr bool valid() const noexcept { return generation != 0; }
        constexpr explicit operator bool() const noexcept { return generation != 0; }

        constexpr std::uint64_t bits() const noexcept
        {
            return (static_cast<std::uint64_t>(generation) << 32) | index;
        }
        static constexpr Handle from_bits(std::uint64_t b) noexcept
        {
            return Handle(static_cast<std::uint32_t>(b),
                          static_cast<std::uint32_t>(b >> 32));
        }

        std::uint64_t hash() const noexcept { return detail::hash_mix(bits()); }

        bool operator==(const Handle &o) const noexcept
        {
            return index == o.index && generation == o.generation;
        }
        bool operator!=(const Handle &o) const noexcept { return !(*this == o); }
    };

    template <typename T, typename Alloc = HeapAlloc>
    class SlotMap
    {
    public:
        using value_type = T;
        using size_type = std::size_t;
        using handle_type = Handle<T>;
        using iterator = T *;
        using const_iterator = const T *;

        SlotMap() noexcept : free_head_(kNoFree) {}

        size_type size() const noexcept { return dense_.size(); }
        bool empty() const noexcept { return dense_.empty(); }
        size_type capacity() const noexcept { return dense_.capacity(); }
        size_type slot_count() const noexcept { return slots_.size(); }

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
            Slot &s = slots_[h.index];
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
            free_head_ = h.index;
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
            return (h.generation & 1u) != 0 && h.index < slots_.size() &&
                   slots_[h.index].generation == h.generation;
        }

        T *get(handle_type h) noexcept
        {
            return contains(h) ? &dense_[slots_[h.index].dense] : nullptr;
        }

        const T *get(handle_type h) const noexcept
        {
            return contains(h) ? &dense_[slots_[h.index].dense] : nullptr;
        }

        T &operator[](handle_type h)
        {
            if (CT_UNLIKELY(!contains(h)))
                detail::fatal("ct::SlotMap: handle invalido (objeto ja apagado?)");
            return dense_[slots_[h.index].dense];
        }

        const T &operator[](handle_type h) const
        {
            if (CT_UNLIKELY(!contains(h)))
                detail::fatal("ct::SlotMap: handle invalido (objeto ja apagado?)");
            return dense_[slots_[h.index].dense];
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
                detail::fatal("ct::SlotMap::handle_at: index fora dos limites");
            const std::uint32_t slot = owners_[i];
            return handle_type(slot, slots_[slot].generation);
        }

        size_type index_of(handle_type h) const
        {
            if (CT_UNLIKELY(!contains(h)))
                detail::fatal("ct::SlotMap::index_of: handle invalido");
            return slots_[h.index].dense;
        }

    private:
        struct Slot
        {
            std::uint32_t dense;      
            std::uint32_t generation; 
        };

        static const std::uint32_t kNoFree = 0xFFFFFFFFu;

        std::uint32_t acquire_slot()
        {
            if (free_head_ != kNoFree)
            {
                const std::uint32_t i = free_head_;
                free_head_ = slots_[i].dense;
                ++slots_[i].generation; 
                return i;
            }
            if (CT_UNLIKELY(slots_.size() >= kNoFree))
                detail::fatal("ct::SlotMap: slots esgotados (2^32)");
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