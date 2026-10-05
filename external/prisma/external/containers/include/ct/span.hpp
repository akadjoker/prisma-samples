#pragma once

#include "detail/utils.hpp"

namespace ct
{
    template <typename T>
    class Span
    {
    public:
        using element_type = T;
        using value_type = typename detail::remove_cv<T>::type;
        using size_type = std::size_t;
        using difference_type = std::ptrdiff_t;
        using pointer = T *;
        using reference = T &;
        using iterator = T *;
        using const_iterator = const T *;
        using reverse_iterator = detail::ReverseIt<T *>;

        enum : size_type { npos = static_cast<size_type>(-1) };

        constexpr Span() noexcept : data_(nullptr), size_(0) {}
        constexpr Span(T *p, size_type n) noexcept : data_(p), size_(n) {}
        Span(T *first, T *last) noexcept
            : data_(first), size_(first == last ? 0 : static_cast<size_type>(last - first))
        {
        }

        template <size_type N>
        constexpr Span(T (&arr)[N]) noexcept : data_(arr), size_(N)
        {
        }

        template <typename C,
                  typename = typename detail::enable_if<
                      !detail::is_same<typename detail::bare<C>::type, Span>::value &&
                      std::is_convertible<decltype(detail::declval<C &>().data()),
                                          T *>::value>::type>
        Span(C &c) noexcept : data_(c.data()), size_(c.size())
        {
        }

        template <typename C,
                  typename = typename detail::enable_if<
                      !detail::is_same<typename detail::bare<C>::type, Span>::value &&
                      std::is_convertible<decltype(detail::declval<const C &>().data()),
                                          T *>::value>::type,
                  typename = void>
        Span(const C &c) noexcept : data_(c.data()), size_(c.size())
        {
        }

        constexpr T *data() const noexcept { return data_; }
        constexpr size_type size() const noexcept { return size_; }
        constexpr size_type size_bytes() const noexcept { return size_ * sizeof(T); }
        constexpr bool empty() const noexcept { return size_ == 0; }

        T &operator[](size_type i) const { return data_[i]; }

        T &at(size_type i) const
        {
            if (CT_UNLIKELY(i >= size_))
                detail::fatal("ct::Span::at: index fora dos limites");
            return data_[i];
        }

        T &front() const
        {
            if (CT_UNLIKELY(size_ == 0))
                detail::fatal("ct::Span::front: span vazio");
            return data_[0];
        }

        T &back() const
        {
            if (CT_UNLIKELY(size_ == 0))
                detail::fatal("ct::Span::back: span vazio");
            return data_[size_ - 1];
        }

        constexpr iterator begin() const noexcept { return data_; }
        constexpr iterator end() const noexcept { return size_ ? data_ + size_ : data_; }
        reverse_iterator rbegin() const noexcept { return reverse_iterator(end()); }
        reverse_iterator rend() const noexcept { return reverse_iterator(begin()); }

        Span first(size_type n) const
        {
            if (CT_UNLIKELY(n > size_))
                detail::fatal("ct::Span::first: pedaco maior que o span");
            return Span(data_, n);
        }

        Span last(size_type n) const
        {
            if (CT_UNLIKELY(n > size_))
                detail::fatal("ct::Span::last: pedaco maior que o span");
            return Span(n == size_ ? data_ : data_ + (size_ - n), n);
        }

        Span subspan(size_type offset, size_type n = npos) const
        {
            if (CT_UNLIKELY(offset > size_))
                detail::fatal("ct::Span::subspan: offset fora dos limites");
            const size_type resto = size_ - offset;
            return Span(offset ? data_ + offset : data_, n < resto ? n : resto);
        }

    private:
        T *data_;
        size_type size_;
    };

    template <typename T>
    inline Span<const unsigned char> as_bytes(Span<T> s) noexcept
    {
        return Span<const unsigned char>(reinterpret_cast<const unsigned char *>(s.data()),
                                         s.size_bytes());
    }

    class StringView
    {
    public:
        using size_type = std::size_t;
        using iterator = const char *;
        using const_iterator = const char *;

        enum : size_type { npos = static_cast<size_type>(-1) }; 

        constexpr StringView() noexcept : data_(""), size_(0) {}
        constexpr StringView(const char *s, size_type n) noexcept : data_(s), size_(n) {}
        StringView(const char *s) noexcept
            : data_(s ? s : ""), size_(s ? std::strlen(s) : 0)
        {
        }

        template <typename S,
                  typename = typename detail::enable_if<
                      !detail::is_same<typename detail::bare<S>::type, StringView>::value &&
                      std::is_convertible<decltype(detail::declval<const S &>().data()),
                                          const char *>::value>::type>
        StringView(const S &s) noexcept : data_(s.data()), size_(s.size())
        {
        }

        constexpr const char *data() const noexcept { return data_; }
        constexpr size_type size() const noexcept { return size_; }
        constexpr size_type length() const noexcept { return size_; }
        constexpr bool empty() const noexcept { return size_ == 0; }

        char operator[](size_type i) const { return data_[i]; }

        char at(size_type i) const
        {
            if (CT_UNLIKELY(i >= size_))
                detail::fatal("ct::StringView::at: index fora dos limites");
            return data_[i];
        }

        char front() const
        {
            if (CT_UNLIKELY(size_ == 0))
                detail::fatal("ct::StringView::front: vista vazia");
            return data_[0];
        }

        char back() const
        {
            if (CT_UNLIKELY(size_ == 0))
                detail::fatal("ct::StringView::back: vista vazia");
            return data_[size_ - 1];
        }

        constexpr iterator begin() const noexcept { return data_; }
        constexpr iterator end() const noexcept { return data_ + size_; }

        Span<const char> bytes() const noexcept { return Span<const char>(data_, size_); }

        StringView substr(size_type pos, size_type n = npos) const
        {
            if (CT_UNLIKELY(pos > size_))
                detail::fatal("ct::StringView::substr: pos fora dos limites");
            const size_type resto = size_ - pos;
            return StringView(data_ + pos, n < resto ? n : resto);
        }

        void remove_prefix(size_type n)
        {
            if (n > size_)
                n = size_;
            data_ += n;
            size_ -= n;
        }

        void remove_suffix(size_type n)
        {
            if (n > size_)
                n = size_;
            size_ -= n;
        }

        StringView trimmed() const noexcept
        {
            size_type i = 0, j = size_;
            while (i < j && is_space(data_[i]))
                ++i;
            while (j > i && is_space(data_[j - 1]))
                --j;
            return StringView(data_ + i, j - i);
        }

        bool split_once(char sep, StringView &head, StringView &tail) const noexcept
        {
            const size_type p = find(sep);
            if (p == npos)
            {
                head = *this;
                tail = StringView(data_ + size_, 0);
                return false;
            }
            head = StringView(data_, p);
            tail = StringView(data_ + p + 1, size_ - p - 1);
            return true;
        }

        size_type find(char c, size_type pos = 0) const noexcept
        {
            if (pos >= size_)
                return npos;
            const void *p = std::memchr(data_ + pos, c, size_ - pos);
            return p ? static_cast<size_type>(static_cast<const char *>(p) - data_) : npos;
        }

        size_type find(StringView needle, size_type pos = 0) const noexcept
        {
            if (needle.size_ == 0)
                return pos <= size_ ? pos : npos;
            if (pos >= size_ || needle.size_ > size_ - pos)
                return npos;
            const char *p = data_ + pos;
            const char *fim = data_ + size_ - needle.size_ + 1;
            while (p < fim)
            {
                const void *hit = std::memchr(p, needle.data_[0],
                                              static_cast<size_type>(fim - p));
                if (!hit)
                    return npos;
                p = static_cast<const char *>(hit);
                if (std::memcmp(p, needle.data_, needle.size_) == 0)
                    return static_cast<size_type>(p - data_);
                ++p;
            }
            return npos;
        }

        size_type rfind(char c) const noexcept
        {
            for (size_type i = size_; i > 0; --i)
                if (data_[i - 1] == c)
                    return i - 1;
            return npos;
        }

        bool contains(char c) const noexcept { return find(c) != npos; }
        bool contains(StringView s) const noexcept { return find(s) != npos; }

        bool starts_with(StringView p) const noexcept
        {
            return size_ >= p.size_ && std::memcmp(data_, p.data_, p.size_) == 0;
        }

        bool ends_with(StringView p) const noexcept
        {
            return size_ >= p.size_ &&
                   std::memcmp(data_ + size_ - p.size_, p.data_, p.size_) == 0;
        }

        int compare(StringView o) const noexcept
        {
            const size_type n = size_ < o.size_ ? size_ : o.size_;
            const int c = n ? std::memcmp(data_, o.data_, n) : 0;
            if (c != 0)
                return c;
            return size_ == o.size_ ? 0 : (size_ < o.size_ ? -1 : 1);
        }

    private:
        static bool is_space(char c) noexcept
        {
            return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
        }

        const char *data_;
        size_type size_;
    };

    inline bool operator==(StringView a, StringView b) noexcept
    {
        return a.size() == b.size() &&
               (a.size() == 0 || std::memcmp(a.data(), b.data(), a.size()) == 0);
    }

    template <typename S,
              typename = typename detail::enable_if<
                  !detail::is_same<typename detail::bare<S>::type, StringView>::value &&
                  std::is_convertible<decltype(detail::declval<const S &>().data()),
                                      const char *>::value>::type>
    inline bool operator==(StringView a, const S &b) noexcept
    {
        return a == StringView(b);
    }
    template <typename S,
              typename = typename detail::enable_if<
                  !detail::is_same<typename detail::bare<S>::type, StringView>::value &&
                  std::is_convertible<decltype(detail::declval<const S &>().data()),
                                      const char *>::value>::type>
    inline bool operator!=(StringView a, const S &b) noexcept
    {
        return !(a == StringView(b));
    }
    inline bool operator==(StringView a, const char *b) noexcept
    {
        return a == StringView(b);
    }
    inline bool operator==(const char *a, StringView b) noexcept
    {
        return StringView(a) == b;
    }
    inline bool operator!=(StringView a, const char *b) noexcept { return !(a == b); }
    inline bool operator!=(const char *a, StringView b) noexcept { return !(a == b); }
    inline bool operator!=(StringView a, StringView b) noexcept { return !(a == b); }
    inline bool operator<(StringView a, StringView b) noexcept { return a.compare(b) < 0; }
    inline bool operator>(StringView a, StringView b) noexcept { return b < a; }
    inline bool operator<=(StringView a, StringView b) noexcept { return !(b < a); }
    inline bool operator>=(StringView a, StringView b) noexcept { return !(a < b); }

} 