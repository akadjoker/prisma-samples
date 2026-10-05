
#pragma once

#include "detail/utils.hpp"
#include <new>

namespace ct
{

    namespace detail
    {

        constexpr std::size_t kSmallSort = 24;   
        constexpr std::size_t kRadixMin = 128;   

        template <typename T, typename C>
        inline void insertion_sort(T *lo, T *hi, C cmp)
        {
            for (T *i = lo + 1; i < hi; ++i)
            {
                if (cmp(*i, *(i - 1)))
                {
                    T tmp = detail::move(*i);
                    T *j = i;
                    do
                    {
                        *j = detail::move(*(j - 1));
                        --j;
                    } while (j > lo && cmp(tmp, *(j - 1)));
                    *j = detail::move(tmp);
                }
            }
        }

        template <typename T, typename C>
        inline void sift_down(T *a, std::size_t n, std::size_t i, C cmp)
        {
            for (;;)
            {
                std::size_t l = 2 * i + 1;
                if (l >= n)
                    return;
                std::size_t big = (l + 1 < n && cmp(a[l], a[l + 1])) ? l + 1 : l;
                if (!cmp(a[i], a[big]))
                    return;
                swap_vals(a[i], a[big]);
                i = big;
            }
        }

        template <typename T, typename C>
        inline void heap_sort(T *lo, T *hi, C cmp)
        {
            std::size_t n = static_cast<std::size_t>(hi - lo);
            for (std::size_t i = n / 2; i > 0; --i)
                sift_down(lo, n, i - 1, cmp);
            for (std::size_t i = n; i > 1; --i)
            {
                swap_vals(lo[0], lo[i - 1]);
                sift_down(lo, i - 1, 0, cmp);
            }
        }

        template <typename T, typename C>
        inline T *median3(T *a, T *b, T *c, C cmp)
        {
            if (cmp(*a, *b))
                return cmp(*b, *c) ? b : (cmp(*a, *c) ? c : a);
            return cmp(*a, *c) ? a : (cmp(*b, *c) ? c : b);
        }

        template <typename T, typename C>
        inline void intro_rec(T *lo, T *hi, int depth, C cmp)
        {
            while (static_cast<std::size_t>(hi - lo) > kSmallSort)
            {
                if (depth-- == 0)
                {
                    heap_sort(lo, hi, cmp);
                    return;
                }
                T *mid = lo + (hi - lo) / 2;
                T *pv = median3(lo, mid, hi - 1, cmp);
                swap_vals(*pv, *(hi - 1)); 
                T &pivot = *(hi - 1);
                T *i = lo;
                for (T *j = lo; j < hi - 1; ++j)
                    if (cmp(*j, pivot))
                    {
                        swap_vals(*i, *j);
                        ++i;
                    }
                swap_vals(*i, *(hi - 1));

                if (i - lo < hi - (i + 1))
                {
                    intro_rec(lo, i, depth, cmp);
                    lo = i + 1;
                }
                else
                {
                    intro_rec(i + 1, hi, depth, cmp);
                    hi = i;
                }
            }
            insertion_sort(lo, hi, cmp);
        }

        inline int log2_floor(std::size_t n)
        {
            int r = 0;
            while (n >>= 1)
                ++r;
            return r;
        }

        template <typename T, typename C>
        inline void intro_sort(T *lo, T *hi, C cmp)
        {
            if (hi - lo > 1)
                intro_rec(lo, hi, 2 * log2_floor(static_cast<std::size_t>(hi - lo)), cmp);
        }

        template <typename T>
        inline bool is_sorted_fast(const T *lo, const T *hi)
        {
            for (const T *p = lo + 1; p < hi; ++p)
                if (*p < *(p - 1))
                    return false;
            return true;
        }

        template <typename T, typename U, typename KeyFn>
        inline void radix_sort(T *a, std::size_t n, KeyFn key_of)
        {
            std::size_t bytes = 0;
            if (!checked_mul(n, sizeof(T), bytes))
                fatal("ct::sort: tamanho invalido");
            HeapAlloc allocator;
            T *buf = static_cast<T *>(allocator.allocate(bytes, alignof(T)));
            T *src = a;
            T *dst = buf;
            constexpr int passes = static_cast<int>(sizeof(U));
            std::size_t count[256];
            for (int pass = 0; pass < passes; ++pass)
            {
                int shift = pass * 8;
                std::memset(count, 0, sizeof(count));
                for (std::size_t i = 0; i < n; ++i)
                    ++count[(key_of(src[i]) >> shift) & 0xFF];

                if (count[(key_of(src[0]) >> shift) & 0xFF] == n)
                    continue;
                std::size_t pos = 0;
                for (int b = 0; b < 256; ++b)
                {
                    std::size_t c = count[b];
                    count[b] = pos;
                    pos += c;
                }
                for (std::size_t i = 0; i < n; ++i)
                    dst[count[(key_of(src[i]) >> shift) & 0xFF]++] = src[i];
                T *t = src;
                src = dst;
                dst = t;
            }
            if (src != a)
                std::memcpy(a, src, bytes);
            allocator.deallocate(buf, bytes);
        }

        template <typename T, typename C>
        inline void merge_forward(T *lo, T *mid, T *hi, T *buf, C cmp)
        {
            std::size_t nl = static_cast<std::size_t>(mid - lo);
            for (std::size_t i = 0; i < nl; ++i)
                new (buf + i) T(detail::move(lo[i]));
            T *l = buf;
            T *le = buf + nl;
            T *r = mid;
            T *out = lo;
            while (l < le && r < hi)
            {
                if (cmp(*r, *l))
                    *out++ = detail::move(*r++);
                else
                    *out++ = detail::move(*l++);
            }
            while (l < le)
                *out++ = detail::move(*l++);
            for (std::size_t i = 0; i < nl; ++i)
                buf[i].~T();
        }

        template <typename T, typename C>
        inline void merge_backward(T *lo, T *mid, T *hi, T *buf, C cmp)
        {
            std::size_t nr = static_cast<std::size_t>(hi - mid);
            for (std::size_t i = 0; i < nr; ++i)
                new (buf + i) T(detail::move(mid[i]));
            T *l = mid;
            T *r = buf + nr;
            T *out = hi;
            while (l > lo && r > buf)
            {
                if (cmp(*(r - 1), *(l - 1)))
                    *--out = detail::move(*--l);
                else
                    *--out = detail::move(*--r);
            }
            while (r > buf)
                *--out = detail::move(*--r);
            for (std::size_t i = 0; i < nr; ++i)
                buf[i].~T();
        }

        template <typename T, typename C>
        inline void stable_sort_impl(T *lo, T *hi, C cmp)
        {
            std::size_t n = static_cast<std::size_t>(hi - lo);
            if (n <= kSmallSort)
            {
                insertion_sort(lo, hi, cmp);
                return;
            }
            for (T *p = lo; p < hi; p += kSmallSort)
                insertion_sort(p, (hi - p > static_cast<std::ptrdiff_t>(kSmallSort)) ? p + kSmallSort : hi, cmp);

            std::size_t half = n / 2 + 1;
            std::size_t bytes = 0;
            if (!checked_mul(half, sizeof(T), bytes))
                fatal("ct::stable_sort: tamanho invalido");
            HeapAlloc allocator;
            T *buf = static_cast<T *>(allocator.allocate(bytes, alignof(T)));
            for (std::size_t width = kSmallSort; width < n; width *= 2)
            {
                for (T *p = lo; hi - p > static_cast<std::ptrdiff_t>(width); p += 2 * width)
                {
                    T *mid = p + width;
                    T *end = (hi - p > static_cast<std::ptrdiff_t>(2 * width)) ? p + 2 * width : hi;
                    if (mid - p <= end - mid)
                        merge_forward(p, mid, end, buf, cmp);
                    else
                        merge_backward(p, mid, end, buf, cmp);
                }
            }
            allocator.deallocate(buf, bytes);
        }

    } 

    template <typename It, typename C>
    inline void sort(It first, It last, C cmp)
    {
        if (first == last)
            return;
        detail::intro_sort(&*first, &*first + (last - first), cmp);
    }

    template <typename It>
    inline void sort(It first, It last)
    {
        if (first == last)
            return;
        using T = typename detail::remove_ref<decltype(*first)>::type;
        detail::intro_sort(&*first, &*first + (last - first),
                           [](const T &a, const T &b) { return a < b; });
    }

    template <typename It, typename C>
    inline void stable_sort(It first, It last, C cmp)
    {
        if (first == last)
            return;
        detail::stable_sort_impl(&*first, &*first + (last - first), cmp);
    }

    template <typename It>
    inline void stable_sort(It first, It last)
    {
        if (first == last)
            return;
        using T = typename detail::remove_ref<decltype(*first)>::type;
        detail::stable_sort_impl(&*first, &*first + (last - first),
                                 [](const T &a, const T &b) { return a < b; });
    }

#define CT_RADIX_SORT(T, U, KEY_EXPR)                                          \
    inline void sort(T *first, T *last)                                        \
    {                                                                          \
        std::size_t n = static_cast<std::size_t>(last - first);                \
        if (n < detail::kRadixMin)                                             \
        {                                                                      \
            detail::intro_sort(first, last,                                    \
                               [](T a, T b) { return a < b; });                \
            return;                                                            \
        }                                                                      \
        if (detail::is_sorted_fast(first, last))                               \
            return;                                                            \
        detail::radix_sort<T, U>(first, n, [](T x) -> U { return KEY_EXPR; }); \
    }

    CT_RADIX_SORT(unsigned char, std::uint8_t, static_cast<std::uint8_t>(x))
    CT_RADIX_SORT(unsigned short, std::uint16_t, static_cast<std::uint16_t>(x))
    CT_RADIX_SORT(short, std::uint16_t,
                  static_cast<std::uint16_t>(x) ^ std::uint16_t(0x8000))
    CT_RADIX_SORT(unsigned, std::uint32_t, static_cast<std::uint32_t>(x))
    CT_RADIX_SORT(int, std::uint32_t,
                  static_cast<std::uint32_t>(x) ^ 0x80000000u)
    CT_RADIX_SORT(unsigned long long, std::uint64_t, static_cast<std::uint64_t>(x))
    CT_RADIX_SORT(long long, std::uint64_t,
                  static_cast<std::uint64_t>(x) ^ (std::uint64_t(1) << 63))

    CT_RADIX_SORT(unsigned long, std::uint64_t, static_cast<std::uint64_t>(x))
    CT_RADIX_SORT(long, std::uint64_t,
                  static_cast<std::uint64_t>(x) ^ (std::uint64_t(1) << 63))

    inline void sort(float *first, float *last)
    {
        std::size_t n = static_cast<std::size_t>(last - first);
        if (n < detail::kRadixMin)
        {
            detail::intro_sort(first, last, [](float a, float b) { return a < b; });
            return;
        }
        if (detail::is_sorted_fast(first, last))
            return;
        detail::radix_sort<float, std::uint32_t>(first, n, [](float f) -> std::uint32_t {
            std::uint32_t b;
            std::memcpy(&b, &f, 4);
            return (b & 0x80000000u) ? ~b : (b | 0x80000000u);
        });
    }

    inline void sort(double *first, double *last)
    {
        std::size_t n = static_cast<std::size_t>(last - first);
        if (n < detail::kRadixMin)
        {
            detail::intro_sort(first, last, [](double a, double b) { return a < b; });
            return;
        }
        if (detail::is_sorted_fast(first, last))
            return;
        detail::radix_sort<double, std::uint64_t>(first, n, [](double f) -> std::uint64_t {
            std::uint64_t b;
            std::memcpy(&b, &f, 8);
            return (b & (std::uint64_t(1) << 63)) ? ~b : (b | (std::uint64_t(1) << 63));
        });
    }

#undef CT_RADIX_SORT

} 