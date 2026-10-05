
#pragma once

#include "detail/utils.hpp"
#include "hashmap.hpp" 

namespace ct
{

    template <typename K, typename H = Hash<K>, typename Alloc = HeapAlloc>
    class HashSet : private H, private Alloc
    {
    public:
        using size_type = std::size_t;

        HashSet() noexcept
            : H(), Alloc(), slots_(nullptr), meta_(nullptr), mask_(0), size_(0)
        {
        }

        explicit HashSet(const H &h) noexcept
            : H(h), Alloc(), slots_(nullptr), meta_(nullptr), mask_(0), size_(0)
        {
        }

        HashSet(const H &h, const Alloc &alloc) noexcept
            : H(h), Alloc(alloc), slots_(nullptr), meta_(nullptr), mask_(0), size_(0)
        {
        }

        ~HashSet()
        {
            destroy_all();
            release_slots();
        }

        HashSet(const HashSet &o) : H(static_cast<const H &>(o)), Alloc(static_cast<const Alloc &>(o)),
                                    slots_(nullptr), meta_(nullptr), mask_(0), size_(0)
        {
            if (o.size_)
            {
                reserve(o.size_);
                for (size_type i = 0; i <= o.mask_; ++i)
                    if (o.meta_[i])
                        insert(o.slots_[i]);
            }
        }

        HashSet(HashSet &&o) noexcept
            : H(static_cast<H &&>(o)),
              Alloc(static_cast<Alloc &&>(o)), slots_(o.slots_), meta_(o.meta_), mask_(o.mask_),
              size_(o.size_)
        {
            o.slots_ = nullptr;
            o.meta_ = nullptr;
            o.mask_ = 0;
            o.size_ = 0;
        }

        HashSet &operator=(const HashSet &o)
        {
            if (this != &o)
            {
                clear();
                if (o.size_)
                {
                    reserve(o.size_);
                    for (size_type i = 0; i <= o.mask_; ++i)
                        if (o.meta_[i])
                            insert(o.slots_[i]);
                }
            }
            return *this;
        }

        HashSet &operator=(HashSet &&o) noexcept
        {
            if (this != &o)
            {
                destroy_all();
                release_slots();
                static_cast<H &>(*this) = static_cast<H &&>(o);
                static_cast<Alloc &>(*this) = static_cast<Alloc &&>(o);
                slots_ = o.slots_;
                meta_ = o.meta_;
                mask_ = o.mask_;
                size_ = o.size_;
                o.slots_ = nullptr;
                o.meta_ = nullptr;
                o.mask_ = 0;
                o.size_ = 0;
            }
            return *this;
        }

        size_type size() const noexcept { return size_; }
        bool empty() const noexcept { return size_ == 0; }
        size_type capacity() const noexcept { return slots_ ? mask_ + 1 : 0; }

        bool contains(const K &k) const noexcept
        {
            if (!size_)
                return false;
            return meta_[probe(k)] != 0;
        }

        template <typename Q, typename HH = H, typename = typename HH::is_transparent,
                  typename = typename detail::enable_if<!detail::is_same<Q, K>::value>::type,
                  typename = decltype(detail::declval<const HH &>()(detail::declval<const Q &>())),
                  typename = decltype(HH::equal(detail::declval<const K &>(), detail::declval<const Q &>()))>
        bool contains(const Q &k) const noexcept
        {
            if (!size_)
                return false;
            const H &hasher = *static_cast<const H *>(this);
            size_type i = static_cast<size_type>(hasher(k)) & mask_;
            while (meta_[i])
            {
                if (hasher.equal(slots_[i], k))
                    return true;
                i = (i + 1) & mask_;
            }
            return false;
        }

        template <typename KK>
        bool insert(KK &&k)
        {
            if (CT_UNLIKELY(need_grow()))
                return insert_slow(detail::forward<KK>(k));
            size_type i = probe(k);
            if (meta_[i])
                return false;
            ::new (static_cast<void *>(&slots_[i])) K(detail::forward<KK>(k));
            meta_[i] = 1;
            ++size_;
            return true;
        }

        bool erase(const K &k)
        {
            if (!size_)
                return false;
            size_type i = probe(k);
            if (!meta_[i])
                return false;
            slots_[i].~K();
            size_type j = i;
            for (;;)
            {
                j = (j + 1) & mask_;
                if (!meta_[j])
                    break;
                size_type ideal = static_cast<size_type>(hash_of(slots_[j])) & mask_;
                if (((j - ideal) & mask_) >= ((j - i) & mask_))
                {
                    ::new (static_cast<void *>(&slots_[i])) K(detail::move(slots_[j]));
                    slots_[j].~K();
                    i = j;
                }
            }
            meta_[i] = 0;
            --size_;
            return true;
        }

        void clear()
        {
            if (slots_)
            {
                destroy_all();
                std::memset(meta_, 0, mask_ + 1);
                size_ = 0;
            }
        }

        void reserve(size_type n)
        {
            size_type scaled = 0;
            size_type requested = 0;
            size_type want = 0;
            if (!detail::checked_add(n, n / 3, scaled) ||
                !detail::checked_add(scaled, 1, requested) ||
                !detail::checked_pow2(requested, want))
                detail::fatal("ct::HashSet: capacidade invalida");
            if (want > capacity())
                rehash(want);
        }

        class iterator
        {
            const K *e_;
            const std::uint8_t *m_;
            const std::uint8_t *mend_;

        public:
            iterator(const K *e, const std::uint8_t *m, const std::uint8_t *mend)
                : e_(e), m_(m), mend_(mend)
            {
                skip();
            }
            const K &operator*() const { return *e_; }
            const K *operator->() const { return e_; }
            iterator &operator++()
            {
                ++e_;
                ++m_;
                skip();
                return *this;
            }
            bool operator==(const iterator &o) const { return m_ == o.m_; }
            bool operator!=(const iterator &o) const { return m_ != o.m_; }

        private:
            void skip()
            {
                while (m_ != mend_ && !*m_)
                {
                    ++m_;
                    ++e_;
                }
            }
        };

        iterator begin() const noexcept
        {
            return iterator(slots_, meta_, meta_ ? meta_ + mask_ + 1 : nullptr);
        }
        iterator end() const noexcept
        {
            const std::uint8_t *me = meta_ ? meta_ + mask_ + 1 : nullptr;
            return iterator(slots_ ? slots_ + mask_ + 1 : nullptr, me, me);
        }

    private:
        K *slots_;
        std::uint8_t *meta_; 
        size_type mask_;
        size_type size_;

        template <typename KK>
        CT_NOINLINE bool insert_slow(KK &&k)
        {
            if (slots_)
            {
                size_type i = probe(k);
                if (meta_[i])
                    return false;
            }
            K stable_key(detail::forward<KK>(k));
            grow();
            size_type i = probe(stable_key);
            ::new (static_cast<void *>(&slots_[i])) K(detail::move(stable_key));
            meta_[i] = 1;
            ++size_;
            return true;
        }

        std::uint64_t hash_of(const K &k) const
        {
            return (*static_cast<const H *>(this))(k);
        }

        bool need_grow() const
        {
            if (!slots_)
                return true;
            const size_type capacity = mask_ + 1;
            return size_ == (std::numeric_limits<size_type>::max)() ||
                   size_ + 1 > capacity - capacity / 4;
        }

        size_type probe(const K &k) const
        {
            size_type i = static_cast<size_type>(hash_of(k)) & mask_;
            while (meta_[i])
            {
                if (slots_[i] == k)
                    return i;
                i = (i + 1) & mask_;
            }
            return i;
        }

        CT_NOINLINE void grow()
        {
            const size_type capacity = slots_ ? mask_ + 1 : 16;
            if (capacity > (std::numeric_limits<size_type>::max)() / 2)
                detail::fatal("ct::HashSet: capacidade invalida");
            rehash(slots_ ? capacity * 2 : capacity);
        }

        void rehash(size_type new_cap)
        {
            if (new_cap < 2 || !detail::is_power_of_two(new_cap))
                detail::fatal("ct::HashSet: capacidade invalida");
            K *old_slots = slots_;
            std::uint8_t *old_meta = meta_;
            size_type old_cap = slots_ ? mask_ + 1 : 0;

            size_type slots_bytes = 0;
            size_type bytes = 0;
            if (!detail::checked_mul(new_cap, sizeof(K), slots_bytes) ||
                !detail::checked_add(slots_bytes, new_cap, bytes))
                detail::fatal("ct::HashSet: tamanho invalido");
            void *blk = allocator().allocate(bytes, alignof(K));
            slots_ = static_cast<K *>(blk);
            meta_ = reinterpret_cast<std::uint8_t *>(slots_ + new_cap);
            std::memset(meta_, 0, new_cap);
            mask_ = new_cap - 1;

            for (size_type i = 0; i < old_cap; ++i)
                if (old_meta[i])
                {
                    size_type j = static_cast<size_type>(hash_of(old_slots[i])) & mask_;
                    while (meta_[j])
                        j = (j + 1) & mask_;
                    ::new (static_cast<void *>(&slots_[j])) K(detail::move(old_slots[i]));
                    meta_[j] = 1;
                    old_slots[i].~K();
                }
            if (old_slots)
            {
                size_type old_slots_bytes = 0;
                size_type old_bytes = 0;
                if (!detail::checked_mul(old_cap, sizeof(K), old_slots_bytes) ||
                    !detail::checked_add(old_slots_bytes, old_cap, old_bytes))
                    detail::fatal("ct::HashSet: tamanho invalido");
                allocator().deallocate(old_slots, old_bytes);
            }
        }

        void destroy_all()
        {
            if (slots_ && size_)
                for (size_type i = 0; i <= mask_; ++i)
                    if (meta_[i])
                        slots_[i].~K();
        }

        Alloc &allocator() { return static_cast<Alloc &>(*this); }

        void release_slots()
        {
            if (!slots_)
                return;
            size_type slots_bytes = 0;
            size_type bytes = 0;
            if (!detail::checked_mul(mask_ + 1, sizeof(K), slots_bytes) ||
                !detail::checked_add(slots_bytes, mask_ + 1, bytes))
                detail::fatal("ct::HashSet: tamanho invalido");
            allocator().deallocate(slots_, bytes);
            slots_ = nullptr;
            meta_ = nullptr;
            mask_ = 0;
        }
    };

} 