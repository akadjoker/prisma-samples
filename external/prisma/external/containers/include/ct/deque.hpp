
#pragma once

#include <initializer_list>

#include "detail/utils.hpp"

namespace ct
{

    template <typename T, typename Alloc = HeapAlloc>
    class Deque : private Alloc
    {
        static_assert(!detail::is_const<T>::value, "ct::Deque<const T> is not allowed");

        using trivial_copy = detail::is_trivially_copyable_t<T>;
        using trivial_dtor = detail::is_trivially_destructible_t<T>;

    public:
        using value_type = T;
        using size_type = std::size_t;
        using difference_type = std::ptrdiff_t;
        using reference = T &;
        using const_reference = const T &;
        using pointer = T *;
        using const_pointer = const T *;

        template <typename Ref, typename Ptr>
        class It
        {
            Ptr base_;
            size_type i_;
            size_type mask_;

            template <typename, typename>
            friend class It;

        public:
            using iterator_category = std::random_access_iterator_tag;
            using value_type = T;
            using difference_type = std::ptrdiff_t;
            using pointer = Ptr;
            using reference = Ref;

            It() noexcept : base_(nullptr), i_(0), mask_(0) {}
            It(Ptr b, size_type i, size_type m) noexcept : base_(b), i_(i), mask_(m) {}
            It(const It &) noexcept = default;
            It &operator=(const It &) noexcept = default;
            template <typename R2, typename P2,
                      typename = typename detail::enable_if<
                          !detail::is_same<It<R2, P2>, It>::value &&
                          std::is_convertible<P2, Ptr>::value>::type>
            It(const It<R2, P2> &o) noexcept : base_(o.base_), i_(o.i_), mask_(o.mask_) {}

            Ref operator*() const { return base_[i_ & mask_]; }
            Ptr operator->() const { return base_ + (i_ & mask_); }
            Ref operator[](difference_type n) const
            {
                return base_[(i_ + static_cast<size_type>(n)) & mask_];
            }

            It &operator++() { ++i_; return *this; }
            It operator++(int) { It t = *this; ++i_; return t; }
            It &operator--() { --i_; return *this; }
            It operator--(int) { It t = *this; --i_; return t; }
            It &operator+=(difference_type n) { i_ += static_cast<size_type>(n); return *this; }
            It &operator-=(difference_type n) { i_ -= static_cast<size_type>(n); return *this; }
            It operator+(difference_type n) const { It t = *this; t += n; return t; }
            It operator-(difference_type n) const { It t = *this; t -= n; return t; }

            template <typename R2, typename P2>
            difference_type operator-(const It<R2, P2> &o) const
            {
                return static_cast<difference_type>(i_) - static_cast<difference_type>(o.i_);
            }

            template <typename R2, typename P2>
            bool operator==(const It<R2, P2> &o) const { return i_ == o.i_; }
            template <typename R2, typename P2>
            bool operator!=(const It<R2, P2> &o) const { return i_ != o.i_; }
            template <typename R2, typename P2>
            bool operator<(const It<R2, P2> &o) const { return i_ < o.i_; }
            template <typename R2, typename P2>
            bool operator<=(const It<R2, P2> &o) const { return i_ <= o.i_; }
            template <typename R2, typename P2>
            bool operator>(const It<R2, P2> &o) const { return i_ > o.i_; }
            template <typename R2, typename P2>
            bool operator>=(const It<R2, P2> &o) const { return i_ >= o.i_; }
        };

        using iterator = It<T &, T *>;
        using const_iterator = It<const T &, const T *>;
        using reverse_iterator = detail::ReverseIt<iterator>;
        using const_reverse_iterator = detail::ReverseIt<const_iterator>;

        Deque() noexcept : data_(nullptr), head_(0), size_(0), cap_(0) {}

        explicit Deque(const Alloc &alloc) noexcept
            : Alloc(alloc), data_(nullptr), head_(0), size_(0), cap_(0) {}

        explicit Deque(size_type n, const Alloc &alloc = Alloc()) : Deque(alloc)
        {
            resize(n);
        }

        Deque(size_type n, const T &value, const Alloc &alloc = Alloc()) : Deque(alloc)
        {
            resize(n, value);
        }

        Deque(std::initializer_list<T> il, const Alloc &alloc = Alloc()) : Deque(alloc)
        {
            reserve(il.size());
            for (const T &v : il)
                push_back(v);
        }

        Deque(const Deque &other)
            : Alloc(static_cast<const Alloc &>(other)),
              data_(nullptr), head_(0), size_(0), cap_(0)
        {
            copy_from(other);
        }

        Deque(Deque &&other) noexcept
            : Alloc(static_cast<Alloc &&>(other)),
              data_(other.data_), head_(other.head_), size_(other.size_), cap_(other.cap_)
        {
            other.data_ = nullptr;
            other.head_ = 0;
            other.size_ = 0;
            other.cap_ = 0;
        }

        ~Deque()
        {
            destroy_all();
            if (data_)
                this->deallocate(data_, cap_ * sizeof(T));
        }

        Deque &operator=(const Deque &other)
        {
            if (this != &other)
            {
                clear();
                copy_from(other);
            }
            return *this;
        }

        Deque &operator=(Deque &&other) noexcept
        {
            if (this != &other)
            {
                destroy_all();
                if (data_)
                    this->deallocate(data_, cap_ * sizeof(T));
                static_cast<Alloc &>(*this) = static_cast<Alloc &&>(other);
                data_ = other.data_;
                head_ = other.head_;
                size_ = other.size_;
                cap_ = other.cap_;
                other.data_ = nullptr;
                other.head_ = 0;
                other.size_ = 0;
                other.cap_ = 0;
            }
            return *this;
        }

        Deque &operator=(std::initializer_list<T> il)
        {
            clear();
            reserve(il.size());
            for (const T &v : il)
                push_back(v);
            return *this;
        }

        reference operator[](size_type i) { return data_[(head_ + i) & (cap_ - 1)]; }
        const_reference operator[](size_type i) const { return data_[(head_ + i) & (cap_ - 1)]; }

        reference at(size_type i)
        {
            if (CT_UNLIKELY(i >= size_))
                detail::fatal("ct::Deque::at: index fora dos limites");
            return (*this)[i];
        }
        const_reference at(size_type i) const
        {
            if (CT_UNLIKELY(i >= size_))
                detail::fatal("ct::Deque::at: index fora dos limites");
            return (*this)[i];
        }

        reference front() { return data_[head_]; }
        const_reference front() const { return data_[head_]; }
        reference back() { return data_[(head_ + size_ - 1) & (cap_ - 1)]; }
        const_reference back() const { return data_[(head_ + size_ - 1) & (cap_ - 1)]; }

        struct Span
        {
            T *ptr;
            size_type len;
        };
        struct ConstSpan
        {
            const T *ptr;
            size_type len;
        };

        Span first_span() noexcept { return Span{data_ ? data_ + head_ : nullptr, first_seg()}; }
        ConstSpan first_span() const noexcept
        {
            return ConstSpan{data_ ? data_ + head_ : nullptr, first_seg()};
        }
        Span second_span() noexcept { return Span{data_, size_ - first_seg()}; }
        ConstSpan second_span() const noexcept { return ConstSpan{data_, size_ - first_seg()}; }

        iterator begin() noexcept { return iterator(data_, head_, cap_ ? cap_ - 1 : 0); }
        const_iterator begin() const noexcept
        {
            return const_iterator(data_, head_, cap_ ? cap_ - 1 : 0);
        }
        const_iterator cbegin() const noexcept { return begin(); }
        iterator end() noexcept { return iterator(data_, head_ + size_, cap_ ? cap_ - 1 : 0); }
        const_iterator end() const noexcept
        {
            return const_iterator(data_, head_ + size_, cap_ ? cap_ - 1 : 0);
        }
        const_iterator cend() const noexcept { return end(); }
        reverse_iterator rbegin() noexcept { return reverse_iterator(end()); }
        const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); }
        reverse_iterator rend() noexcept { return reverse_iterator(begin()); }
        const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); }

        bool empty() const noexcept { return size_ == 0; }
        size_type size() const noexcept { return size_; }
        size_type capacity() const noexcept { return cap_; }
        size_type max_size() const noexcept
        {
            return (std::numeric_limits<size_type>::max)() / sizeof(T);
        }

        void reserve(size_type n)
        {
            if (n > max_size())
                detail::fatal("ct::Deque: capacidade invalida");
            if (n > cap_)
            {
                size_type capacity = 0;
                if (!detail::checked_pow2(n, capacity))
                    detail::fatal("ct::Deque: capacidade invalida");
                change_capacity(capacity);
            }
        }

        void shrink_to_fit()
        {
            if (size_ == 0)
            {
                if (data_)
                {
                    this->deallocate(data_, cap_ * sizeof(T));
                    data_ = nullptr;
                    head_ = 0;
                    cap_ = 0;
                }
                return;
            }
            size_type want = 0;
            if (!detail::checked_pow2(size_, want))
                detail::fatal("ct::Deque: capacidade invalida");
            if (want < cap_)
                change_capacity(want);
        }

        void clear() noexcept
        {
            destroy_all();
            head_ = 0;
            size_ = 0;
        }

        void push_back(const T &value)
        {
            if (CT_UNLIKELY(size_ == cap_))
            {
                push_back_cold(value, trivial_copy{});
                return;
            }
            ::new (static_cast<void *>(data_ + ((head_ + size_) & (cap_ - 1)))) T(value);
            ++size_;
        }

        void push_back(T &&value)
        {
            if (CT_UNLIKELY(size_ == cap_))
            {
                push_back_slow(detail::move(value));
                return;
            }
            ::new (static_cast<void *>(data_ + ((head_ + size_) & (cap_ - 1))))
                T(detail::move(value));
            ++size_;
        }

        template <typename... Args>
        reference emplace_back(Args &&...args)
        {
            if (CT_UNLIKELY(size_ == cap_))
            {
                T stable(detail::forward<Args>(args)...);
                grow();
                T *p = ::new (static_cast<void *>(data_ + ((head_ + size_) & (cap_ - 1))))
                    T(detail::move(stable));
                ++size_;
                return *p;
            }
            T *p = ::new (static_cast<void *>(data_ + ((head_ + size_) & (cap_ - 1))))
                T(detail::forward<Args>(args)...);
            ++size_;
            return *p;
        }

        void push_front(const T &value)
        {
            if (CT_UNLIKELY(size_ == cap_))
            {
                push_front_cold(value, trivial_copy{});
                return;
            }
            head_ = (head_ - 1) & (cap_ - 1);
            ::new (static_cast<void *>(data_ + head_)) T(value);
            ++size_;
        }

        void push_front(T &&value)
        {
            if (CT_UNLIKELY(size_ == cap_))
            {
                push_front_slow(detail::move(value));
                return;
            }
            head_ = (head_ - 1) & (cap_ - 1);
            ::new (static_cast<void *>(data_ + head_)) T(detail::move(value));
            ++size_;
        }

        template <typename... Args>
        reference emplace_front(Args &&...args)
        {
            if (CT_UNLIKELY(size_ == cap_))
            {
                T stable(detail::forward<Args>(args)...);
                grow();
                head_ = (head_ - 1) & (cap_ - 1);
                T *p = ::new (static_cast<void *>(data_ + head_)) T(detail::move(stable));
                ++size_;
                return *p;
            }
            head_ = (head_ - 1) & (cap_ - 1);
            T *p = ::new (static_cast<void *>(data_ + head_)) T(detail::forward<Args>(args)...);
            ++size_;
            return *p;
        }

        void pop_back()
        {
            --size_;
            detail::destroy_n(data_ + ((head_ + size_) & (cap_ - 1)), 1, trivial_dtor{});
        }

        void pop_front()
        {
            detail::destroy_n(data_ + head_, 1, trivial_dtor{});
            head_ = (head_ + 1) & (cap_ - 1);
            --size_;
        }

        void resize(size_type n)
        {
            while (size_ > n)
                pop_back();
            if (n > size_)
            {
                reserve(n);
                while (size_ < n)
                    emplace_back();
            }
        }

        void resize(size_type n, const T &value)
        {
            while (size_ > n)
                pop_back();
            if (n > size_)
            {
                if (n > cap_ && contains_address(&value))
                {
                    T stable(value);
                    reserve(n);
                    while (size_ < n)
                        push_back(stable);
                }
                else
                {
                    reserve(n);
                    while (size_ < n)
                        push_back(value);
                }
            }
        }

        void swap(Deque &other) noexcept
        {
            detail::swap_vals(static_cast<Alloc &>(*this), static_cast<Alloc &>(other));
            detail::swap_vals(data_, other.data_);
            detail::swap_vals(head_, other.head_);
            detail::swap_vals(size_, other.size_);
            detail::swap_vals(cap_, other.cap_);
        }

        const Alloc &get_allocator() const noexcept
        {
            return static_cast<const Alloc &>(*this);
        }

    private:
        T *data_;
        size_type head_; 
        size_type size_;
        size_type cap_; 

        void push_back_cold(T value, detail::true_type)
        {
            grow();
            data_[(head_ + size_) & (cap_ - 1)] = value;
            ++size_;
        }
        void push_back_cold(const T &value, detail::false_type) { push_back_slow(value); }

        void push_front_cold(T value, detail::true_type)
        {
            grow();
            head_ = (head_ - 1) & (cap_ - 1);
            data_[head_] = value;
            ++size_;
        }
        void push_front_cold(const T &value, detail::false_type) { push_front_slow(value); }

        template <typename V>
        void push_back_slow(V &&value)
        {
            if (contains_address(&value))
            {
                T stable(detail::forward<V>(value));
                grow();
                ::new (static_cast<void *>(data_ + ((head_ + size_) & (cap_ - 1))))
                    T(detail::move(stable));
            }
            else
            {
                grow();
                ::new (static_cast<void *>(data_ + ((head_ + size_) & (cap_ - 1))))
                    T(detail::forward<V>(value));
            }
            ++size_;
        }

        template <typename V>
        void push_front_slow(V &&value)
        {
            if (contains_address(&value))
            {
                T stable(detail::forward<V>(value));
                grow();
                head_ = (head_ - 1) & (cap_ - 1);
                ::new (static_cast<void *>(data_ + head_)) T(detail::move(stable));
            }
            else
            {
                grow();
                head_ = (head_ - 1) & (cap_ - 1);
                ::new (static_cast<void *>(data_ + head_)) T(detail::forward<V>(value));
            }
            ++size_;
        }

        bool contains_address(const T *value) const noexcept
        {
            if (!data_ || !value)
                return false;
            const std::uintptr_t begin = reinterpret_cast<std::uintptr_t>(data_);
            const std::uintptr_t end = begin + cap_ * sizeof(T);
            const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(value);
            return address >= begin && address < end;
        }

        size_type first_seg() const noexcept
        {
            size_type tail_room = cap_ - head_;
            return size_ < tail_room ? size_ : tail_room;
        }

        void destroy_all() noexcept
        {
            if (!data_)
                return;
            size_type f = first_seg();
            detail::destroy_n(data_ + head_, f, trivial_dtor{});
            detail::destroy_n(data_, size_ - f, trivial_dtor{});
        }

        void copy_from(const Deque &other)
        {
            reserve(other.size_);
            for (size_type i = 0; i < other.size_; ++i)
                ::new (static_cast<void *>(data_ + i)) T(other[i]);
            head_ = 0;
            size_ = other.size_;
        }

        void grow()
        {
            if (cap_ > max_size() / 2)
                detail::fatal("ct::Deque: capacidade invalida");
            change_capacity(cap_ ? cap_ * 2 : 8);
        }

        void change_capacity(size_type new_cap) 
        {
            if (new_cap == 0 || new_cap > max_size() || !detail::is_power_of_two(new_cap))
                detail::fatal("ct::Deque: capacidade invalida");
            reallocate_impl(new_cap, trivial_copy{});
        }

        void reallocate_impl(size_type new_cap, detail::true_type)
        {

            if (data_ && cap_ <= max_size() / 2 && new_cap >= cap_ * 2)
            {
                size_type old_bytes = 0;
                size_type new_bytes = 0;
                if (!detail::checked_mul(cap_, sizeof(T), old_bytes) ||
                    !detail::checked_mul(new_cap, sizeof(T), new_bytes))
                    detail::fatal("ct::Deque: tamanho invalido");
                data_ = static_cast<T *>(Alloc::reallocate(data_, old_bytes, new_bytes, alignof(T)));
                size_type tail = size_ > cap_ - head_ ? size_ - (cap_ - head_) : 0;
                if (tail)
                    std::memcpy(static_cast<void *>(data_ + cap_),
                                static_cast<const void *>(data_), tail * sizeof(T));
                cap_ = new_cap;
                return;
            }
            move_to_fresh(new_cap, detail::true_type{});
        }

        void reallocate_impl(size_type new_cap, detail::false_type)
        {
            move_to_fresh(new_cap, detail::false_type{});
        }

        template <typename Trivial>
        void move_to_fresh(size_type new_cap, Trivial t)
        {
            size_type bytes = 0;
            if (!detail::checked_mul(new_cap, sizeof(T), bytes))
                detail::fatal("ct::Deque: tamanho invalido");
            T *p = static_cast<T *>(this->allocate(bytes, alignof(T)));
            size_type f = data_ ? first_seg() : 0;
            detail::relocate_n(p, data_ ? data_ + head_ : nullptr, f, t);
            detail::relocate_n(p + f, data_, size_ - f, t);
            if (data_)
            {
                size_type old_bytes = 0;
                if (!detail::checked_mul(cap_, sizeof(T), old_bytes))
                    detail::fatal("ct::Deque: tamanho invalido");
                this->deallocate(data_, old_bytes);
            }
            data_ = p;
            head_ = 0;
            cap_ = new_cap;
        }
    };

    template <typename T, typename A1, typename A2>
    inline bool operator==(const Deque<T, A1> &a, const Deque<T, A2> &b)
    {
        if (a.size() != b.size())
            return false;
        for (typename Deque<T, A1>::size_type i = 0; i < a.size(); ++i)
            if (!(a[i] == b[i]))
                return false;
        return true;
    }

    template <typename T, typename A1, typename A2>
    inline bool operator!=(const Deque<T, A1> &a, const Deque<T, A2> &b)
    {
        return !(a == b);
    }

    template <typename T, typename A>
    inline void swap(Deque<T, A> &a, Deque<T, A> &b) noexcept
    {
        a.swap(b);
    }

}
