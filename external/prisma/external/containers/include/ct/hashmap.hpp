
#pragma once

#include "detail/utils.hpp"
#include "span.hpp"
#include "string.hpp"

namespace ct
{

    template <typename K>
    struct Hash
    {
        std::uint64_t operator()(const K &k) const
        {
            return call(k, detail::integral_constant<bool, std::is_enum<K>::value>());
        }

    private:
        static std::uint64_t call(const K &k, detail::false_type) { return k.hash(); }
        static std::uint64_t call(const K &k, detail::true_type)
        {
            return detail::hash_mix(static_cast<std::uint64_t>(
                static_cast<typename std::underlying_type<K>::type>(k)));
        }
    };

    inline std::uint64_t hash_combine(std::uint64_t seed, std::uint64_t h) noexcept
    {
        return seed ^ (h + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2));
    }

#define CT_INT_HASH(T)                                                    \
    template <>                                                           \
    struct Hash<T>                                                        \
    {                                                                     \
        std::uint64_t operator()(T k) const                               \
        {                                                                 \
            return detail::hash_mix(static_cast<std::uint64_t>(k));      \
        }                                                                 \
    };
    CT_INT_HASH(char)
    CT_INT_HASH(signed char)
    CT_INT_HASH(unsigned char)
    CT_INT_HASH(short)
    CT_INT_HASH(unsigned short)
    CT_INT_HASH(int)
    CT_INT_HASH(unsigned)
    CT_INT_HASH(long)
    CT_INT_HASH(unsigned long)
    CT_INT_HASH(long long)
    CT_INT_HASH(unsigned long long)
    CT_INT_HASH(wchar_t)
    CT_INT_HASH(char16_t)
    CT_INT_HASH(char32_t)
#undef CT_INT_HASH

    template <>
    struct Hash<bool>
    {
        std::uint64_t operator()(bool k) const { return detail::hash_mix(k ? 1u : 0u); }
    };

    template <>
    struct Hash<float>
    {
        std::uint64_t operator()(float k) const
        {
            if (k == 0.0f)
                k = 0.0f;
            std::uint32_t bits;
            std::memcpy(&bits, &k, sizeof(bits));
            return detail::hash_mix(bits);
        }
    };

    template <>
    struct Hash<double>
    {
        std::uint64_t operator()(double k) const
        {
            if (k == 0.0)
                k = 0.0;
            std::uint64_t bits;
            std::memcpy(&bits, &k, sizeof(bits));
            return detail::hash_mix(bits);
        }
    };

    template <>
    struct Hash<StringView>
    {
        std::uint64_t operator()(StringView s) const noexcept
        {
            return detail::hash_bytes(s.data(), s.size());
        }
    };

    template <>
    struct Hash<String>
    {
        using is_transparent = void;

        std::uint64_t operator()(const String &s) const noexcept
        {
            return detail::hash_bytes(s.data(), s.size());
        }
        std::uint64_t operator()(StringView s) const noexcept
        {
            return detail::hash_bytes(s.data(), s.size());
        }
        std::uint64_t operator()(const char *s) const noexcept { return (*this)(StringView(s)); }

        static bool equal(const String &a, StringView b) noexcept
        {
            return a.size() == b.size() && (b.size() == 0 || std::memcmp(a.data(), b.data(), b.size()) == 0);
        }
        static bool equal(const String &a, const char *b) noexcept { return equal(a, StringView(b)); }
    };

    template <typename T>
    struct Hash<T *>
    {
        std::uint64_t operator()(T *p) const
        {
            return detail::hash_mix(reinterpret_cast<std::uintptr_t>(p));
        }
    };

    template <typename K, typename V, typename H = Hash<K>, typename Alloc = HeapAlloc>
    class HashMap : private H, private Alloc
    {
    public:
        struct Entry
        {
            K key;
            V value;
        };

        using size_type = std::size_t;

        HashMap() noexcept
            : H(), Alloc(), slots_(nullptr), meta_(nullptr), mask_(0), size_(0)
        {
        }

        explicit HashMap(const H &h) noexcept
            : H(h), Alloc(), slots_(nullptr), meta_(nullptr), mask_(0), size_(0)
        {
        }

        HashMap(const H &h, const Alloc &alloc) noexcept
            : H(h), Alloc(alloc), slots_(nullptr), meta_(nullptr), mask_(0), size_(0)
        {
        }

        ~HashMap()
        {
            destroy_all();
            release_slots();
        }

        HashMap(const HashMap &o) : H(static_cast<const H &>(o)), Alloc(static_cast<const Alloc &>(o)),
                                    slots_(nullptr), meta_(nullptr), mask_(0), size_(0)
        {
            if (o.size_)
            {
                reserve(o.size_);
                for (size_type i = 0; i <= o.mask_; ++i)
                    if (o.meta_[i])
                        put(o.slots_[i].key, o.slots_[i].value);
            }
        }

        HashMap(HashMap &&o) noexcept
            : H(static_cast<H &&>(o)),
              Alloc(static_cast<Alloc &&>(o)), slots_(o.slots_), meta_(o.meta_), mask_(o.mask_),
              size_(o.size_)
        {
            o.slots_ = nullptr;
            o.meta_ = nullptr;
            o.mask_ = 0;
            o.size_ = 0;
        }

        HashMap &operator=(const HashMap &o)
        {
            if (this != &o)
            {
                clear();
                if (o.size_)
                {
                    reserve(o.size_);
                    for (size_type i = 0; i <= o.mask_; ++i)
                        if (o.meta_[i])
                            put(o.slots_[i].key, o.slots_[i].value);
                }
            }
            return *this;
        }

        HashMap &operator=(HashMap &&o) noexcept
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

        V *find(const K &k) noexcept
        {
            if (!size_)
                return nullptr;
            size_type i = probe(k);
            return meta_[i] ? &slots_[i].value : nullptr;
        }
        const V *find(const K &k) const noexcept
        {
            return const_cast<HashMap *>(this)->find(k);
        }

        template <typename Q, typename HH = H, typename = typename HH::is_transparent,
                  typename = typename detail::enable_if<!detail::is_same<Q, K>::value>::type,
                  typename = decltype(detail::declval<const HH &>()(detail::declval<const Q &>())),
                  typename = decltype(HH::equal(detail::declval<const K &>(), detail::declval<const Q &>()))>
        V *find(const Q &k) noexcept
        {
            if (!size_)
                return nullptr;
            size_type i = probe_as(k);
            return meta_[i] ? &slots_[i].value : nullptr;
        }
        template <typename Q, typename HH = H, typename = typename HH::is_transparent,
                  typename = typename detail::enable_if<!detail::is_same<Q, K>::value>::type,
                  typename = decltype(detail::declval<const HH &>()(detail::declval<const Q &>())),
                  typename = decltype(HH::equal(detail::declval<const K &>(), detail::declval<const Q &>()))>
        const V *find(const Q &k) const noexcept
        {
            return const_cast<HashMap *>(this)->find(k);
        }

        bool contains(const K &k) const noexcept { return find(k) != nullptr; }

        template <typename Q, typename HH = H, typename = typename HH::is_transparent,
                  typename = typename detail::enable_if<!detail::is_same<Q, K>::value>::type,
                  typename = decltype(detail::declval<const HH &>()(detail::declval<const Q &>())),
                  typename = decltype(HH::equal(detail::declval<const K &>(), detail::declval<const Q &>()))>
        bool contains(const Q &k) const noexcept
        {
            return find(k) != nullptr;
        }

        const V &get(const K &k, const V &fallback) const noexcept
        {
            const V *v = find(k);
            return v ? *v : fallback;
        }

        template <typename KK, typename VV>
        V &put(KK &&k, VV &&v)
        {
            if (CT_UNLIKELY(need_grow()))
                return put_slow(detail::forward<KK>(k), detail::forward<VV>(v));
            size_type i = probe(k);
            if (meta_[i])
                slots_[i].value = detail::forward<VV>(v);
            else
            {
                ::new (static_cast<void *>(&slots_[i]))
                    Entry{detail::forward<KK>(k), detail::forward<VV>(v)};
                meta_[i] = 1;
                ++size_;
            }
            return slots_[i].value;
        }

        V &operator[](const K &k)
        {
            if (CT_UNLIKELY(need_grow()))
                return index_slow(k);
            size_type i = probe(k);
            if (!meta_[i])
            {
                ::new (static_cast<void *>(&slots_[i])) Entry{k, V()};
                meta_[i] = 1;
                ++size_;
            }
            return slots_[i].value;
        }

        /* Removes k and, when it was present, moves its value into out. Saves
           the caller a second probe when the removed value is still needed. */
        bool erase(const K &k, V &out)
        {
            if (!size_)
                return false;
            size_type i = probe(k);
            if (!meta_[i])
                return false;
            out = detail::move(slots_[i].value);
            erase_at(i);
            return true;
        }

        bool erase(const K &k)
        {
            if (!size_)
                return false;
            size_type i = probe(k);
            if (!meta_[i])
                return false;
            erase_at(i);
            return true;
        }

        void erase_at(size_type i)
        {
            slots_[i].~Entry();

            size_type j = i;
            for (;;)
            {
                j = (j + 1) & mask_;
                if (!meta_[j])
                    break;
                size_type ideal = static_cast<size_type>(hash_of(slots_[j].key)) & mask_;

                if (((j - ideal) & mask_) >= ((j - i) & mask_))
                {
                    ::new (static_cast<void *>(&slots_[i]))
                        Entry{detail::move(slots_[j].key), detail::move(slots_[j].value)};
                    slots_[j].~Entry();
                    i = j;
                }
            }
            meta_[i] = 0;
            --size_;
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
                detail::fatal("ct::HashMap: capacidade invalida");
            if (want > capacity())
                rehash(want);
        }

        class const_iterator
        {
            const Entry *e_;
            const std::uint8_t *m_;
            const std::uint8_t *mend_;

        public:
            const_iterator(const Entry *e, const std::uint8_t *m, const std::uint8_t *mend)
                : e_(e), m_(m), mend_(mend)
            {
                skip();
            }
            const Entry &operator*() const { return *e_; }
            const Entry *operator->() const { return e_; }
            const_iterator &operator++()
            {
                ++e_;
                ++m_;
                skip();
                return *this;
            }
            bool operator==(const const_iterator &o) const { return m_ == o.m_; }
            bool operator!=(const const_iterator &o) const { return m_ != o.m_; }

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

        class iterator
        {
            Entry *e_;
            std::uint8_t *m_;
            std::uint8_t *mend_;

        public:
            iterator(Entry *e, std::uint8_t *m, std::uint8_t *mend)
                : e_(e), m_(m), mend_(mend)
            {
                skip();
            }
            Entry &operator*() const { return *e_; }
            Entry *operator->() const { return e_; }
            iterator &operator++()
            {
                ++e_;
                ++m_;
                skip();
                return *this;
            }
            bool operator==(const iterator &o) const { return m_ == o.m_; }
            bool operator!=(const iterator &o) const { return m_ != o.m_; }

            operator const_iterator() const { return const_iterator(e_, m_, mend_); }

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

        iterator begin() noexcept
        {
            return iterator(slots_, meta_, meta_ ? meta_ + mask_ + 1 : nullptr);
        }
        iterator end() noexcept
        {
            std::uint8_t *me = meta_ ? meta_ + mask_ + 1 : nullptr;
            return iterator(slots_ ? slots_ + mask_ + 1 : nullptr, me, me);
        }
        const_iterator begin() const noexcept
        {
            return const_iterator(slots_, meta_, meta_ ? meta_ + mask_ + 1 : nullptr);
        }
        const_iterator end() const noexcept
        {
            const std::uint8_t *me = meta_ ? meta_ + mask_ + 1 : nullptr;
            return const_iterator(slots_ ? slots_ + mask_ + 1 : nullptr, me, me);
        }
        const_iterator cbegin() const noexcept { return begin(); }
        const_iterator cend() const noexcept { return end(); }

    private:
        Entry *slots_;
        std::uint8_t *meta_; 
        size_type mask_;     
        size_type size_;

        template <typename KK, typename VV>
        CT_NOINLINE V &put_slow(KK &&k, VV &&v)
        {
            if (slots_ && size_ < mask_ + 1) /* at least one empty slot: probe terminates */
            {
                size_type i = probe(k);
                if (meta_[i])
                {
                    slots_[i].value = detail::forward<VV>(v);
                    return slots_[i].value;
                }
            }
            K stable_key(detail::forward<KK>(k));
            V stable_value(detail::forward<VV>(v));
            grow();
            size_type i = probe(stable_key);
            ::new (static_cast<void *>(&slots_[i]))
                Entry{detail::move(stable_key), detail::move(stable_value)};
            meta_[i] = 1;
            ++size_;
            return slots_[i].value;
        }

        CT_NOINLINE V &index_slow(const K &k)
        {
            if (slots_ && size_ < mask_ + 1) /* at least one empty slot: probe terminates */
            {
                size_type i = probe(k);
                if (meta_[i])
                    return slots_[i].value;
            }
            K stable_key(k);
            grow();
            size_type i = probe(stable_key);
            ::new (static_cast<void *>(&slots_[i])) Entry{detail::move(stable_key), V()};
            meta_[i] = 1;
            ++size_;
            return slots_[i].value;
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
            /* A table must NEVER be 100% full: probe() stops only at the key
               or at an empty slot, so a full table and a missing key spin
               forever. For capacity 2 (reserve(1), e.g. a copy of a 1-entry
               map) `capacity - capacity / 4` IS the whole table, which let
               the second insert fill it and the third hang. */
            return size_ == (std::numeric_limits<size_type>::max)() ||
                   size_ + 1 >= capacity ||
                   size_ + 1 > capacity - capacity / 4;
        }

        template <typename Q>
        size_type probe_as(const Q &k) const
        {
            const H &hasher = *static_cast<const H *>(this);
            size_type i = static_cast<size_type>(hasher(k)) & mask_;
            while (meta_[i])
            {
                if (hasher.equal(slots_[i].key, k))
                    return i;
                i = (i + 1) & mask_;
            }
            return i;
        }

        size_type probe(const K &k) const
        {
            size_type i = static_cast<size_type>(hash_of(k)) & mask_;
            while (meta_[i])
            {
                if (slots_[i].key == k)
                    return i;
                i = (i + 1) & mask_;
            }
            return i;
        }

        CT_NOINLINE void grow()
        {
            const size_type capacity = slots_ ? mask_ + 1 : 16;
            if (capacity > (std::numeric_limits<size_type>::max)() / 2)
                detail::fatal("ct::HashMap: capacidade invalida");
            rehash(slots_ ? capacity * 2 : capacity);
        }

        void rehash(size_type new_cap)
        {
            if (new_cap < 2 || !detail::is_power_of_two(new_cap))
                detail::fatal("ct::HashMap: capacidade invalida");
            Entry *old_slots = slots_;
            std::uint8_t *old_meta = meta_;
            size_type old_cap = slots_ ? mask_ + 1 : 0;

            size_type entries_bytes = 0;
            size_type bytes = 0;
            if (!detail::checked_mul(new_cap, sizeof(Entry), entries_bytes) ||
                !detail::checked_add(entries_bytes, new_cap, bytes))
                detail::fatal("ct::HashMap: tamanho invalido");
            void *blk = allocator().allocate(bytes, alignof(Entry));
            slots_ = static_cast<Entry *>(blk);
            meta_ = reinterpret_cast<std::uint8_t *>(slots_ + new_cap);
            std::memset(meta_, 0, new_cap);
            mask_ = new_cap - 1;

            for (size_type i = 0; i < old_cap; ++i)
                if (old_meta[i])
                {
                    size_type j = static_cast<size_type>(hash_of(old_slots[i].key)) & mask_;
                    while (meta_[j])
                        j = (j + 1) & mask_;
                    ::new (static_cast<void *>(&slots_[j]))
                        Entry{detail::move(old_slots[i].key),
                              detail::move(old_slots[i].value)};
                    meta_[j] = 1;
                    old_slots[i].~Entry();
                }
            if (old_slots)
            {
                size_type old_entries_bytes = 0;
                size_type old_bytes = 0;
                if (!detail::checked_mul(old_cap, sizeof(Entry), old_entries_bytes) ||
                    !detail::checked_add(old_entries_bytes, old_cap, old_bytes))
                    detail::fatal("ct::HashMap: tamanho invalido");
                allocator().deallocate(old_slots, old_bytes);
            }
        }

        void destroy_all()
        {
            if (slots_ && size_)
                for (size_type i = 0; i <= mask_; ++i)
                    if (meta_[i])
                        slots_[i].~Entry();
        }

        Alloc &allocator() { return static_cast<Alloc &>(*this); }

        void release_slots()
        {
            if (!slots_)
                return;
            size_type entries_bytes = 0;
            size_type bytes = 0;
            if (!detail::checked_mul(mask_ + 1, sizeof(Entry), entries_bytes) ||
                !detail::checked_add(entries_bytes, mask_ + 1, bytes))
                detail::fatal("ct::HashMap: tamanho invalido");
            allocator().deallocate(slots_, bytes);
            slots_ = nullptr;
            meta_ = nullptr;
            mask_ = 0;
        }
    };

}
