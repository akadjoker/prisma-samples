
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <limits>
#include <new> 
#include <type_traits>

#if defined(__GNUC__) || defined(__clang__)
#define CT_LIKELY(x) __builtin_expect(!!(x), 1)
#define CT_UNLIKELY(x) __builtin_expect(!!(x), 0)
#define CT_NOINLINE __attribute__((noinline))
#define CT_FORCEINLINE inline __attribute__((always_inline))
#else
#define CT_LIKELY(x) (x)
#define CT_UNLIKELY(x) (x)
#define CT_NOINLINE
#define CT_FORCEINLINE inline
#endif

namespace ct
{
    namespace detail
    {

        template <typename T, T V>
        struct integral_constant
        {
            static constexpr T value = V;
            using value_type = T;
            using type = integral_constant;
            constexpr operator T() const noexcept { return V; }
        };

        using true_type = integral_constant<bool, true>;
        using false_type = integral_constant<bool, false>;

        template <bool B, typename T = void>
        struct enable_if
        {
        };
        template <typename T>
        struct enable_if<true, T>
        {
            using type = T;
        };

        template <typename A, typename B>
        struct is_same : false_type
        {
        };
        template <typename A>
        struct is_same<A, A> : true_type
        {
        };

        template <typename T>
        struct remove_ref
        {
            using type = T;
        };
        template <typename T>
        struct remove_ref<T &>
        {
            using type = T;
        };
        template <typename T>
        struct remove_ref<T &&>
        {
            using type = T;
        };

        template <typename T>
        struct remove_cv
        {
            using type = T;
        };
        template <typename T>
        struct remove_cv<const T>
        {
            using type = T;
        };
        template <typename T>
        struct remove_cv<volatile T>
        {
            using type = T;
        };
        template <typename T>
        struct remove_cv<const volatile T>
        {
            using type = T;
        };

        template <typename T>
        struct bare
        {
            using type = typename remove_cv<typename remove_ref<T>::type>::type;
        };

        template <typename T>
        struct is_const : false_type
        {
        };
        template <typename T>
        struct is_const<const T> : true_type
        {
        };

        template <typename T>
        using is_trivially_copyable_t = integral_constant<bool, std::is_trivially_copyable<T>::value>;

        template <typename T>
        using is_trivially_destructible_t =
            integral_constant<bool, std::is_trivially_destructible<T>::value>;

        template <typename T>
        struct is_bytewise_comparable
            : integral_constant<bool, std::is_integral<T>::value ||
                                          std::is_enum<T>::value ||
                                          std::is_pointer<T>::value>
        {
        };

        template <typename T>
        struct is_memcmp_ordered
            : integral_constant<bool, std::is_integral<T>::value && sizeof(T) == 1 &&
                                          !std::is_signed<T>::value>
        {
        };

        template <typename T>
        constexpr typename remove_ref<T>::type &&move(T &&t) noexcept
        {
            return static_cast<typename remove_ref<T>::type &&>(t);
        }

        template <typename T>
        constexpr T &&forward(typename remove_ref<T>::type &t) noexcept
        {
            return static_cast<T &&>(t);
        }
        template <typename T>
        constexpr T &&forward(typename remove_ref<T>::type &&t) noexcept
        {
            return static_cast<T &&>(t);
        }

        template <typename T>
        T &&declval() noexcept; 

        template <typename T>
        inline void swap_vals(T &a, T &b)
        {
            T t = detail::move(a);
            a = detail::move(b);
            b = detail::move(t);
        }

        [[noreturn]] inline void fatal(const char *msg)
        {
            std::fputs(msg, stderr);
            std::fputc('\n', stderr);
            std::abort();
        }

        template <typename It>
        class ReverseIt
        {
            It it_;

        public:
            using iterator_category = typename std::iterator_traits<It>::iterator_category;
            using value_type = typename std::iterator_traits<It>::value_type;
            using difference_type = typename std::iterator_traits<It>::difference_type;
            using pointer = typename std::iterator_traits<It>::pointer;
            using reference = typename std::iterator_traits<It>::reference;

            ReverseIt() : it_() {}
            explicit ReverseIt(It it) : it_(it) {}
            reference operator*() const
            {
                It t = it_;
                return *--t;
            }
            pointer operator->() const
            {
                It t = it_;
                --t;
                return &*t;
            }
            reference operator[](difference_type n) const { return *(*this + n); }
            ReverseIt &operator++()
            {
                --it_;
                return *this;
            }
            ReverseIt operator++(int)
            {
                ReverseIt t = *this;
                --it_;
                return t;
            }
            ReverseIt &operator--()
            {
                ++it_;
                return *this;
            }
            ReverseIt operator--(int)
            {
                ReverseIt t = *this;
                ++it_;
                return t;
            }
            ReverseIt &operator+=(difference_type n)
            {
                it_ -= n;
                return *this;
            }
            ReverseIt &operator-=(difference_type n)
            {
                it_ += n;
                return *this;
            }
            ReverseIt operator+(difference_type n) const { return ReverseIt(it_ - n); }
            ReverseIt operator-(difference_type n) const { return ReverseIt(it_ + n); }
            difference_type operator-(const ReverseIt &o) const { return o.it_ - it_; }
            It base() const { return it_; }
            bool operator==(const ReverseIt &o) const { return it_ == o.it_; }
            bool operator!=(const ReverseIt &o) const { return it_ != o.it_; }
            bool operator<(const ReverseIt &o) const { return o.it_ < it_; }
            bool operator>(const ReverseIt &o) const { return o.it_ > it_; }
            bool operator<=(const ReverseIt &o) const { return o.it_ <= it_; }
            bool operator>=(const ReverseIt &o) const { return o.it_ >= it_; }
        };

        template <typename T>
        inline void relocate_n(T *dst, T *src, std::size_t n, true_type )
        {
            if (n)
                std::memcpy(static_cast<void *>(dst), static_cast<const void *>(src),
                            n * sizeof(T));
        }

        template <typename T>
        inline void relocate_n(T *dst, T *src, std::size_t n, false_type)
        {
            for (std::size_t i = 0; i < n; ++i)
            {
                ::new (static_cast<void *>(dst + i)) T(detail::move(src[i]));
                src[i].~T();
            }
        }

        template <typename T>
        inline void relocate_n(T *dst, T *src, std::size_t n)
        {
            relocate_n(dst, src, n, is_trivially_copyable_t<T>{});
        }

        template <typename T>
        inline void destroy_n(T *, std::size_t, true_type ) {}

        template <typename T>
        inline void destroy_n(T *p, std::size_t n, false_type)
        {
            for (std::size_t i = n; i > 0; --i)
                p[i - 1].~T();
        }

        template <typename T>
        inline void destroy_n(T *p, std::size_t n)
        {
            destroy_n(p, n, is_trivially_destructible_t<T>{});
        }

        template <typename T>
        inline void copy_construct_n(T *dst, const T *src, std::size_t n, true_type)
        {
            if (n)
                std::memcpy(static_cast<void *>(dst), static_cast<const void *>(src),
                            n * sizeof(T));
        }

        template <typename T>
        inline void copy_construct_n(T *dst, const T *src, std::size_t n, false_type)
        {
            for (std::size_t i = 0; i < n; ++i)
                ::new (static_cast<void *>(dst + i)) T(src[i]);
        }

        template <typename T>
        inline void copy_construct_n(T *dst, const T *src, std::size_t n)
        {
            copy_construct_n(dst, src, n, is_trivially_copyable_t<T>{});
        }

        template <typename T>
        inline void fill_construct_n(T *dst, std::size_t n, const T &value)
        {
            for (std::size_t i = 0; i < n; ++i)
                ::new (static_cast<void *>(dst + i)) T(value);
        }

        template <typename T>
        CT_NOINLINE void fill_assign_n(T *dst, std::size_t n, T value)
        {
            for (std::size_t i = 0; i < n; ++i)
                dst[i] = value;
        }

        template <typename T>
        inline void fill_trivial_n(T *dst, std::size_t n, const T &v, true_type )
        {
            unsigned char byte;
            std::memcpy(&byte, static_cast<const void *>(&v), 1);
            if (n)
                std::memset(static_cast<void *>(dst), byte, n);
        }

        template <typename T>
        inline void fill_trivial_n(T *dst, std::size_t n, const T &v, false_type)
        {
            const T tmp = v;
            for (std::size_t i = 0; i < n; ++i)
                dst[i] = tmp;
        }

        template <typename T>
        inline void fill_fast_n(T *dst, std::size_t n, const T &v, true_type )
        {
            fill_trivial_n(dst, n, v, integral_constant<bool, sizeof(T) == 1>{});
        }

        template <typename T>
        inline void fill_fast_n(T *dst, std::size_t n, const T &v, false_type)
        {
            for (std::size_t i = 0; i < n; ++i)
                dst[i] = v;
        }

        template <typename T>
        inline void fill_fast_n(T *dst, std::size_t n, const T &v)
        {
            fill_fast_n(dst, n, v, is_trivially_copyable_t<T>{});
        }

        template <typename T>
        inline bool equal_n(const T *a, const T *b, std::size_t n, true_type )
        {
            return n == 0 || std::memcmp(a, b, n * sizeof(T)) == 0;
        }

        template <typename T>
        inline bool equal_n(const T *a, const T *b, std::size_t n, false_type)
        {
            for (std::size_t i = 0; i < n; ++i)
                if (!(a[i] == b[i]))
                    return false;
            return true;
        }

        template <typename T>
        inline bool equal_n(const T *a, const T *b, std::size_t n)
        {
            return equal_n(a, b, n, is_bytewise_comparable<T>{});
        }

        template <typename T>
        inline bool lex_less_n(const T *a, std::size_t na, const T *b, std::size_t nb,
                               true_type )
        {
            const std::size_t n = na < nb ? na : nb;
            const int c = n ? std::memcmp(a, b, n) : 0;
            return c < 0 || (c == 0 && na < nb);
        }

        template <typename T>
        inline bool lex_less_n(const T *a, std::size_t na, const T *b, std::size_t nb,
                               false_type)
        {
            const std::size_t n = na < nb ? na : nb;
            for (std::size_t i = 0; i < n; ++i)
            {
                if (a[i] < b[i])
                    return true;
                if (b[i] < a[i])
                    return false;
            }
            return na < nb;
        }

        template <typename T>
        inline bool lex_less_n(const T *a, std::size_t na, const T *b, std::size_t nb)
        {
            return lex_less_n(a, na, b, nb, is_memcmp_ordered<T>{});
        }

        inline std::uint64_t hash_mix(std::uint64_t x) noexcept
        {
            x ^= x >> 33;
            x *= 0xff51afd7ed558ccdull;
            x ^= x >> 33;
            return x;
        }

        inline std::uint64_t hash_bytes(const char *data, std::size_t n) noexcept
        {
            std::uint64_t result = 1469598103934665603ull;
            for (std::size_t i = 0; i < n; ++i)
            {
                result ^= static_cast<unsigned char>(data[i]);
                result *= 1099511628211ull;
            }
            return result;
        }

        inline std::size_t next_pow2(std::size_t n)
        {
            if (n < 2)
                return 2;
            if (n > (std::numeric_limits<std::size_t>::max)() / 2)
                return 0;
            --n;
            n |= n >> 1;
            n |= n >> 2;
            n |= n >> 4;
            n |= n >> 8;
            n |= n >> 16;
#if SIZE_MAX > 0xffffffff
            n |= n >> 32;
#endif
            return n + 1;
        }

        inline bool is_power_of_two(std::size_t value)
        {
            return value != 0 && (value & (value - 1)) == 0;
        }

        inline bool checked_add(std::size_t a, std::size_t b, std::size_t &result)
        {
            if (b > (std::numeric_limits<std::size_t>::max)() - a)
                return false;
            result = a + b;
            return true;
        }

        inline bool checked_mul(std::size_t a, std::size_t b, std::size_t &result)
        {
            if (a != 0 && b > (std::numeric_limits<std::size_t>::max)() / a)
                return false;
            result = a * b;
            return true;
        }

        inline bool checked_pow2(std::size_t value, std::size_t &result)
        {
            if (value < 2)
            {
                result = 2;
                return true;
            }
            if (value > (std::numeric_limits<std::size_t>::max)() / 2)
                return false;
            result = next_pow2(value);
            return result >= value;
        }

    } 

    template <typename K>
    struct Less
    {
        bool operator()(const K &a, const K &b) const { return a < b; }
    };

    struct HeapAlloc
    {
        struct Header
        {
            void *raw;
        };

        void *allocate(std::size_t bytes, std::size_t align)
        {
            if (!detail::is_power_of_two(align))
                detail::fatal("ct::HeapAlloc: alinhamento invalido");

            if (align < alignof(Header))
                align = alignof(Header);
            if (bytes == 0)
                bytes = 1;

            if (align == alignof(Header))
            {
                std::size_t total = 0;
                if (!detail::checked_add(sizeof(Header), bytes, total))
                    detail::fatal("ct::HeapAlloc: tamanho invalido");
                void *raw = std::malloc(total);
                if (CT_UNLIKELY(!raw))
                    detail::fatal("ct::HeapAlloc: sem memoria");
                Header *header = static_cast<Header *>(raw);
                header->raw = raw;
                return static_cast<void *>(header + 1);
            }

            std::size_t total = 0;
            if (!detail::checked_add(sizeof(Header), bytes, total) ||
                !detail::checked_add(total, align - 1, total))
                detail::fatal("ct::HeapAlloc: tamanho invalido");

            void *raw = std::malloc(total);
            if (CT_UNLIKELY(!raw))
                detail::fatal("ct::HeapAlloc: sem memoria");

            const std::uintptr_t start = reinterpret_cast<std::uintptr_t>(raw) + sizeof(Header);
            const std::uintptr_t aligned = (start + align - 1) & ~(align - 1);
            Header *header = reinterpret_cast<Header *>(aligned - sizeof(Header));
            header->raw = raw;
            return reinterpret_cast<void *>(aligned);
        }

        void deallocate(void *p, std::size_t )
        {
            if (!p)
                return;
            Header *header = reinterpret_cast<Header *>(reinterpret_cast<std::uintptr_t>(p) -
                                                        sizeof(Header));
            std::free(header->raw);
        }

        void *reallocate(void *p, std::size_t old_bytes, std::size_t new_bytes,
                         std::size_t align)
        {
            if (!p)
                return allocate(new_bytes, align);
            if (new_bytes == 0)
                new_bytes = 1;

            if (align < alignof(Header))
                align = alignof(Header);
            if (align == alignof(Header))
            {
                Header *header = reinterpret_cast<Header *>(reinterpret_cast<std::uintptr_t>(p) -
                                                              sizeof(Header));
                std::size_t total = 0;
                if (!detail::checked_add(sizeof(Header), new_bytes, total))
                    detail::fatal("ct::HeapAlloc: tamanho invalido");
                void *raw = std::realloc(header->raw, total);
                if (CT_UNLIKELY(!raw))
                    detail::fatal("ct::HeapAlloc: sem memoria");
                Header *new_header = static_cast<Header *>(raw);
                new_header->raw = raw;
                return static_cast<void *>(new_header + 1);
            }

            void *q = allocate(new_bytes, align);
            const std::size_t copied = old_bytes < new_bytes ? old_bytes : new_bytes;
            if (copied)
                std::memcpy(q, p, copied);
            deallocate(p, old_bytes);
            return q;
        }
    };

} 