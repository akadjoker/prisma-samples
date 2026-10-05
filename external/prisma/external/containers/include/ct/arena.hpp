#pragma once

#include "detail/utils.hpp"

namespace ct
{

    template <typename Alloc = HeapAlloc>
    class BasicArena : private Alloc
    {
        struct Block
        {
            Block *prev;
            char *data;
            std::size_t size;
            std::size_t allocation_size;
        };

    public:
        explicit BasicArena(std::size_t first_block_bytes = 64 * 1024,
                            const Alloc &alloc = Alloc())
            : Alloc(alloc), head_(nullptr), cur_(nullptr), end_(nullptr), last_(nullptr),
              next_size_(first_block_bytes ? first_block_bytes : 1), used_(0), reserved_(0)
        {
            new_block(next_size_, alignof(std::max_align_t));
        }

        ~BasicArena() { release(); }

        BasicArena(const BasicArena &) = delete;
        BasicArena &operator=(const BasicArena &) = delete;
        BasicArena(BasicArena &&) = delete;
        BasicArena &operator=(BasicArena &&) = delete;

        void *allocate(std::size_t bytes, std::size_t align = alignof(std::max_align_t))
        {
            validate_alignment(align);
            char *aligned = aligned_address(cur_, align);
            if (!cur_ || aligned > end_ || bytes > static_cast<std::size_t>(end_ - aligned))
            {
                new_block(bytes, align);
                aligned = aligned_address(cur_, align);
            }
            cur_ = aligned + bytes;
            last_ = aligned;
            if (bytes > (std::numeric_limits<std::size_t>::max)() - used_)
                detail::fatal("ct::Arena: tamanho invalido");
            used_ += bytes;
            return aligned;
        }

        bool try_expand(void *p, std::size_t old_bytes, std::size_t new_bytes)
        {
            if (!p || new_bytes < old_bytes || static_cast<char *>(p) != last_ ||
                new_bytes > static_cast<std::size_t>(end_ - static_cast<char *>(p)))
                return false;
            const std::size_t delta = new_bytes - old_bytes;
            if (delta > (std::numeric_limits<std::size_t>::max)() - used_)
                return false;
            cur_ = static_cast<char *>(p) + new_bytes;
            used_ += delta;
            return true;
        }

        void *reallocate(void *p, std::size_t old_bytes, std::size_t new_bytes,
                         std::size_t align = alignof(std::max_align_t))
        {
            if (p && new_bytes >= old_bytes && try_expand(p, old_bytes, new_bytes))
                return p;
            void *result = allocate(new_bytes, align);
            if (p && old_bytes && new_bytes)
            {
                const std::size_t copied = old_bytes < new_bytes ? old_bytes : new_bytes;
                std::memcpy(result, p, copied);
            }
            return result;
        }

        template <typename T, typename... Args>
        T *create(Args &&...args)
        {
            return ::new (allocate(sizeof(T), alignof(T))) T(detail::forward<Args>(args)...);
        }

        template <typename T>
        T *allocate_array(std::size_t n)
        {
            std::size_t bytes = 0;
            if (!detail::checked_mul(n, sizeof(T), bytes))
                detail::fatal("ct::Arena: tamanho invalido");
            return static_cast<T *>(allocate(bytes, alignof(T)));
        }

        void reset()
        {
            if (!head_)
            {
                new_block(next_size_, alignof(std::max_align_t));
            }
            else if (head_->prev)
            {
                const std::size_t total = reserved_;
                free_blocks();
                reserved_ = 0;
                new_block(total ? total : next_size_, alignof(std::max_align_t));
            }
            else
            {
                cur_ = head_->data;
                end_ = cur_ + head_->size;
            }
            last_ = nullptr;
            used_ = 0;
        }

        void release()
        {
            free_blocks();
            used_ = reserved_ = 0;
        }

        bool owns(const void *p) const
        {
            const std::uintptr_t pointer = reinterpret_cast<std::uintptr_t>(p);
            for (Block *block = head_; block; block = block->prev)
            {
                const std::uintptr_t begin = reinterpret_cast<std::uintptr_t>(block->data);
                const std::uintptr_t end = begin + block->size;
                if (pointer >= begin && pointer < end)
                    return true;
            }
            return false;
        }

        std::size_t bytes_used() const { return used_; }
        std::size_t bytes_reserved() const { return reserved_; }

    private:
        Alloc &allocator() { return static_cast<Alloc &>(*this); }

        Block *head_;
        char *cur_;
        char *end_;
        char *last_;
        std::size_t next_size_;
        std::size_t used_;
        std::size_t reserved_;

        static void validate_alignment(std::size_t align)
        {
            if (!detail::is_power_of_two(align))
                detail::fatal("ct::Arena: alinhamento invalido");
        }

        static char *aligned_address(char *p, std::size_t align)
        {
            if (!p)
                return nullptr;
            const std::uintptr_t value = reinterpret_cast<std::uintptr_t>(p);
            if (value > (std::numeric_limits<std::uintptr_t>::max)() - (align - 1))
                detail::fatal("ct::Arena: endereco invalido");
            return reinterpret_cast<char *>((value + align - 1) & ~(align - 1));
        }

        void free_blocks()
        {
            Block *block = head_;
            while (block)
            {
                Block *previous = block->prev;
                allocator().deallocate(block, block->allocation_size);
                block = previous;
            }
            head_ = nullptr;
            cur_ = end_ = last_ = nullptr;
        }

        CT_NOINLINE void new_block(std::size_t min_bytes, std::size_t align)
        {
            validate_alignment(align);
            std::size_t size = next_size_;
            if (size < min_bytes)
                size = min_bytes;
            std::size_t total = 0;
            if (!detail::checked_add(sizeof(Block), align - 1, total) ||
                !detail::checked_add(total, size, total))
                detail::fatal("ct::Arena: tamanho invalido");
            if (size > (std::numeric_limits<std::size_t>::max)() - reserved_)
                detail::fatal("ct::Arena: tamanho invalido");

            Block *block = static_cast<Block *>(allocator().allocate(total, alignof(Block)));
            block->prev = head_;
            block->data = aligned_address(reinterpret_cast<char *>(block) + sizeof(Block), align);
            block->size = size;
            block->allocation_size = total;
            head_ = block;
            cur_ = block->data;
            end_ = cur_ + size;
            reserved_ += size;
            next_size_ = size > (std::numeric_limits<std::size_t>::max)() / 2
                             ? size
                             : size * 2;
        }
    };

    using Arena = BasicArena<HeapAlloc>;

    template <typename ArenaType>
    struct BasicArenaAlloc
    {
        ArenaType *arena;

        explicit BasicArenaAlloc(ArenaType &value) : arena(&value) {}

        void *allocate(std::size_t bytes, std::size_t align)
        {
            return arena->allocate(bytes, align);
        }
        void deallocate(void *, std::size_t) {}
        void *reallocate(void *p, std::size_t old_bytes, std::size_t new_bytes,
                         std::size_t align)
        {
            return arena->reallocate(p, old_bytes, new_bytes, align);
        }
    };

    using ArenaAlloc = BasicArenaAlloc<Arena>;

} 