#pragma once

#include "detail/utils.hpp"

namespace ct
{

    template <typename T, typename Alloc = HeapAlloc>
    class Pool : private Alloc
    {
        struct FreeSlot
        {
            FreeSlot *next;
        };

        struct Chunk
        {
            Chunk *next;
            char *data;
            std::uint8_t *states;
        };

        static constexpr std::size_t kAlign =
            alignof(T) > alignof(FreeSlot *) ? alignof(T) : alignof(FreeSlot *);
        static constexpr std::size_t kRaw = sizeof(T) > sizeof(FreeSlot *) ? sizeof(T) : sizeof(FreeSlot *);
        static constexpr std::size_t kSlot = (kRaw + kAlign - 1) & ~(kAlign - 1);

        static constexpr std::size_t default_slots()
        {
            return (16 * 1024) / kSlot ? (16 * 1024) / kSlot : 1;
        }

    public:
        explicit Pool(std::size_t slots_per_chunk = default_slots(), const Alloc &alloc = Alloc())
            : Alloc(alloc), chunks_(nullptr), index_(nullptr), index_size_(0), index_cap_(0), free_(nullptr), cur_(nullptr),
              end_(nullptr), slots_per_chunk_(slots_per_chunk ? slots_per_chunk : 1), live_(0),
              capacity_(0)
        {
            if (slots_per_chunk_ > max_slots())
                detail::fatal("ct::Pool: numero de slots invalido");
        }

        ~Pool()
        {
            destroy_live();
            release_chunks();
        }

        Pool(const Pool &) = delete;
        Pool &operator=(const Pool &) = delete;

        Pool(Pool &&o) noexcept
            : Alloc(static_cast<Alloc &&>(o)), chunks_(o.chunks_), index_(o.index_),
              index_size_(o.index_size_), index_cap_(o.index_cap_), free_(o.free_), cur_(o.cur_), end_(o.end_), slots_per_chunk_(o.slots_per_chunk_),
              live_(o.live_), capacity_(o.capacity_)
        {
            o.chunks_ = nullptr;
            o.index_ = nullptr;
            o.index_size_ = o.index_cap_ = 0;
            o.free_ = nullptr;
            o.cur_ = o.end_ = nullptr;
            o.live_ = 0;
            o.capacity_ = 0;
        }

        Pool &operator=(Pool &&o) noexcept
        {
            if (this != &o)
            {
                destroy_live();
                release_chunks();
                static_cast<Alloc &>(*this) = static_cast<Alloc &&>(o);
                chunks_ = o.chunks_;
                index_ = o.index_;
                index_size_ = o.index_size_;
                index_cap_ = o.index_cap_;
                free_ = o.free_;
                cur_ = o.cur_;
                end_ = o.end_;
                slots_per_chunk_ = o.slots_per_chunk_;
                live_ = o.live_;
                capacity_ = o.capacity_;
                o.chunks_ = nullptr;
                o.index_ = nullptr;
                o.index_size_ = o.index_cap_ = 0;
                o.free_ = nullptr;
                o.cur_ = o.end_ = nullptr;
                o.live_ = 0;
                o.capacity_ = 0;
            }
            return *this;
        }

        T *allocate()
        {
            FreeSlot *slot = free_;
            if (slot)
            {
                free_ = slot->next;
                set_state(reinterpret_cast<T *>(slot), 0);
                ++live_;
                return reinterpret_cast<T *>(slot);
            }
            if (CT_UNLIKELY(cur_ == end_))
                new_chunk();
            T *result = reinterpret_cast<T *>(cur_);
            cur_ += kSlot;
            set_state(result, 0);
            ++live_;
            return result;
        }

        void deallocate(T *p)
        {
            if (!p || state(p) == 2)
                detail::fatal("ct::Pool: slot invalido");
            set_state(p, 2);
            FreeSlot *slot = reinterpret_cast<FreeSlot *>(p);
            slot->next = free_;
            free_ = slot;
            --live_;
        }

        template <typename... Args>
        T *create(Args &&...args)
        {
            T *result = allocate();
            ::new (static_cast<void *>(result)) T(detail::forward<Args>(args)...);
            set_state(result, 1);
            return result;
        }

        void destroy(T *p)
        {
            if (!p || state(p) != 1)
                detail::fatal("ct::Pool: objeto invalido");
            p->~T();
            deallocate(p);
        }

        void clear()
        {
            destroy_live();
            free_ = nullptr;
            cur_ = end_ = nullptr;
            for (Chunk *chunk = chunks_; chunk; chunk = chunk->next)
            {
                for (std::size_t i = 0; i < slots_per_chunk_; ++i)
                {
                    T *slot = reinterpret_cast<T *>(chunk->data + i * kSlot);
                    set_state(slot, 2);
                    FreeSlot *free_slot = reinterpret_cast<FreeSlot *>(slot);
                    free_slot->next = free_;
                    free_ = free_slot;
                }
            }
            live_ = 0;
        }

        std::size_t live() const { return live_; }
        std::size_t capacity() const { return capacity_; }
        static constexpr std::size_t slot_size() { return kSlot; }

    private:
        Chunk *chunks_;
        Chunk **index_;
        std::size_t index_size_;
        std::size_t index_cap_;
        FreeSlot *free_;
        char *cur_;
        char *end_;
        std::size_t slots_per_chunk_;
        std::size_t live_;
        std::size_t capacity_;

        static constexpr std::size_t max_slots()
        {
            return (std::numeric_limits<std::size_t>::max)() / kSlot;
        }

        static char *aligned_data(void *memory)
        {
            const std::uintptr_t value = reinterpret_cast<std::uintptr_t>(memory) + sizeof(Chunk);
            return reinterpret_cast<char *>((value + kAlign - 1) & ~(kAlign - 1));
        }

        static std::uintptr_t address(const void *p) noexcept
        {
            return reinterpret_cast<std::uintptr_t>(p);
        }

        std::size_t index_upper_bound(std::uintptr_t pointer) const noexcept
        {
            std::size_t lo = 0;
            std::size_t hi = index_size_;
            while (lo < hi)
            {
                const std::size_t mid = lo + (hi - lo) / 2;
                if (pointer < address(index_[mid]->data))
                    hi = mid;
                else
                    lo = mid + 1;
            }
            return lo;
        }

        Chunk *find_chunk(const T *p) const
        {
            const std::uintptr_t pointer = address(p);
            const std::size_t at = index_upper_bound(pointer);
            if (at == 0)
                return nullptr;
            Chunk *chunk = index_[at - 1];
            const std::uintptr_t end = address(chunk->data) + slots_per_chunk_ * kSlot;
            return pointer < end ? chunk : nullptr;
        }

        std::uint8_t &state_ref(T *p) const
        {
            Chunk *chunk = find_chunk(p);
            if (!chunk)
                detail::fatal("ct::Pool: slot fora do pool");
            const std::size_t offset = static_cast<std::size_t>(reinterpret_cast<char *>(p) -
                                                                 chunk->data);
            if (offset % kSlot != 0)
                detail::fatal("ct::Pool: slot desalinhado");
            return chunk->states[offset / kSlot];
        }

        std::uint8_t state(const T *p) const
        {
            return state_ref(const_cast<T *>(p));
        }

        void set_state(T *p, std::uint8_t value) { state_ref(p) = value; }

        void destroy_live()
        {
            for (Chunk *chunk = chunks_; chunk; chunk = chunk->next)
            {
                for (std::size_t i = 0; i < slots_per_chunk_; ++i)
                {
                    if (chunk->states[i] == 1)
                    {
                        T *slot = reinterpret_cast<T *>(chunk->data + i * kSlot);
                        slot->~T();
                        chunk->states[i] = 0;
                    }
                }
            }
            live_ = 0;
        }

        void release_chunks()
        {
            Chunk *chunk = chunks_;
            while (chunk)
            {
                Chunk *next = chunk->next;
                std::size_t slots_bytes = 0;
                std::size_t total = 0;
                if (!detail::checked_mul(slots_per_chunk_, kSlot, slots_bytes) ||
                    !detail::checked_add(sizeof(Chunk), kAlign - 1, total) ||
                    !detail::checked_add(total, slots_bytes, total) ||
                    !detail::checked_add(total, slots_per_chunk_, total))
                    detail::fatal("ct::Pool: tamanho invalido");
                allocator().deallocate(chunk, total);
                chunk = next;
            }
            chunks_ = nullptr;
            if (index_)
                allocator().deallocate(index_, index_cap_ * sizeof(Chunk *));
            index_ = nullptr;
            index_size_ = index_cap_ = 0;
            free_ = nullptr;
            cur_ = end_ = nullptr;
            capacity_ = 0;
        }

        CT_NOINLINE void new_chunk()
        {
            std::size_t slots_bytes = 0;
            std::size_t total = 0;
            if (capacity_ > (std::numeric_limits<std::size_t>::max)() - slots_per_chunk_)
                detail::fatal("ct::Pool: capacidade excedida");
            if (!detail::checked_mul(slots_per_chunk_, kSlot, slots_bytes) ||
                !detail::checked_add(sizeof(Chunk), kAlign - 1, total) ||
                !detail::checked_add(total, slots_bytes, total) ||
                !detail::checked_add(total, slots_per_chunk_, total))
                detail::fatal("ct::Pool: tamanho invalido");
            grow_index();
            void *memory = allocator().allocate(total, alignof(Chunk));
            Chunk *chunk = ::new (memory) Chunk();
            chunk->next = chunks_;
            chunk->data = aligned_data(chunk);
            chunk->states = reinterpret_cast<std::uint8_t *>(chunk->data + slots_bytes);
            std::memset(chunk->states, 2, slots_per_chunk_);
            const std::size_t at = index_upper_bound(address(chunk->data));
            if (at < index_size_)
                std::memmove(index_ + at + 1, index_ + at, (index_size_ - at) * sizeof(Chunk *));
            index_[at] = chunk;
            ++index_size_;
            chunks_ = chunk;
            cur_ = chunk->data;
            end_ = cur_ + slots_bytes;
            capacity_ += slots_per_chunk_;
        }

        void grow_index()
        {
            if (index_size_ < index_cap_)
                return;
            std::size_t cap = index_cap_ ? index_cap_ * 2 : 8;
            std::size_t bytes = 0;
            if (cap < index_cap_ || !detail::checked_mul(cap, sizeof(Chunk *), bytes))
                detail::fatal("ct::Pool: capacidade excedida");
            Chunk **grown = static_cast<Chunk **>(allocator().allocate(bytes, alignof(Chunk *)));
            if (index_)
            {
                std::memcpy(grown, index_, index_size_ * sizeof(Chunk *));
                allocator().deallocate(index_, index_cap_ * sizeof(Chunk *));
            }
            index_ = grown;
            index_cap_ = cap;
        }

        Alloc &allocator() { return static_cast<Alloc &>(*this); }
    };

} 