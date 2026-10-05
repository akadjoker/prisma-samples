
#pragma once

#include <initializer_list>

#include "detail/utils.hpp"

namespace ct
{

    template <typename T, typename Alloc = HeapAlloc>
    class Vector : private Alloc
    {
        static_assert(!detail::is_const<T>::value, "ct::Vector<const T> is not allowed");

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
        using iterator = T *;
        using const_iterator = const T *;
        using reverse_iterator = detail::ReverseIt<iterator>;
        using const_reverse_iterator = detail::ReverseIt<const_iterator>;

        Vector() noexcept : data_(nullptr), end_(nullptr), cap_(nullptr) {}

        explicit Vector(const Alloc &alloc) noexcept
            : Alloc(alloc), data_(nullptr), end_(nullptr), cap_(nullptr) {}

        explicit Vector(size_type n, const Alloc &alloc = Alloc()) : Vector(alloc)
        {
            resize(n);
        }

        Vector(size_type n, const T &value, const Alloc &alloc = Alloc()) : Vector(alloc)
        {
            resize(n, value);
        }

        Vector(std::initializer_list<T> il, const Alloc &alloc = Alloc()) : Vector(alloc)
        {
            reserve(il.size());
            for (const T &v : il)
                ::new (static_cast<void *>(end_++)) T(v);
        }

        template <typename InputIt,
                  typename = decltype(*detail::declval<InputIt &>()),
                  typename = decltype(++detail::declval<InputIt &>())>
        Vector(InputIt first, InputIt last, const Alloc &alloc = Alloc()) : Vector(alloc)
        {
            assign(first, last);
        }

        Vector(const Vector &other)
            : Alloc(static_cast<const Alloc &>(other)),
              data_(nullptr), end_(nullptr), cap_(nullptr)
        {
            reserve(other.size());
            copy_construct_from(other.data_, other.size());
        }

        Vector(Vector &&other) noexcept
            : Alloc(static_cast<Alloc &&>(other)),
              data_(other.data_), end_(other.end_), cap_(other.cap_)
        {
            other.data_ = nullptr;
            other.end_ = nullptr;
            other.cap_ = nullptr;
        }

        ~Vector()
        {
            detail::destroy_n(data_, size(), trivial_dtor{});
            if (data_)
                this->deallocate(data_, capacity() * sizeof(T));
        }

        Vector &operator=(const Vector &other)
        {
            if (this != &other)
            {
                clear();
                reserve(other.size());
                copy_construct_from(other.data_, other.size());
            }
            return *this;
        }

        Vector &operator=(Vector &&other) noexcept
        {
            if (this != &other)
            {
                detail::destroy_n(data_, size(), trivial_dtor{});
                if (data_)
                    this->deallocate(data_, capacity() * sizeof(T));
                static_cast<Alloc &>(*this) = static_cast<Alloc &&>(other);
                data_ = other.data_;
                end_ = other.end_;
                cap_ = other.cap_;
                other.data_ = nullptr;
                other.end_ = nullptr;
                other.cap_ = nullptr;
            }
            return *this;
        }

        Vector &operator=(std::initializer_list<T> il)
        {
            clear();
            reserve(il.size());
            for (const T &v : il)
                ::new (static_cast<void *>(end_++)) T(v);
            return *this;
        }

        template <typename InputIt,
                  typename = decltype(*detail::declval<InputIt &>()),
                  typename = decltype(++detail::declval<InputIt &>())>
        void assign(InputIt first, InputIt last)
        {
            clear();
            for (; first != last; ++first)
                emplace_back(*first);
        }

        void assign(size_type n, const T &value)
        {
            clear();
            resize(n, value);
        }

        reference operator[](size_type i) { return data_[i]; }
        const_reference operator[](size_type i) const { return data_[i]; }

        reference at(size_type i)
        {
            if (CT_UNLIKELY(i >= size()))
                detail::fatal("ct::Vector::at: index fora dos limites");
            return data_[i];
        }
        const_reference at(size_type i) const
        {
            if (CT_UNLIKELY(i >= size()))
                detail::fatal("ct::Vector::at: index fora dos limites");
            return data_[i];
        }

        reference front() { return data_[0]; }
        const_reference front() const { return data_[0]; }
        reference back() { return end_[-1]; }
        const_reference back() const { return end_[-1]; }

        T *data() noexcept { return data_; }
        const T *data() const noexcept { return data_; }

        iterator begin() noexcept { return data_; }
        const_iterator begin() const noexcept { return data_; }
        const_iterator cbegin() const noexcept { return data_; }
        iterator end() noexcept { return end_; }
        const_iterator end() const noexcept { return end_; }
        const_iterator cend() const noexcept { return end_; }
        reverse_iterator rbegin() noexcept { return reverse_iterator(end()); }
        const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); }
        reverse_iterator rend() noexcept { return reverse_iterator(begin()); }
        const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); }

        bool empty() const noexcept { return end_ == data_; }
        size_type size() const noexcept
        {
            return data_ ? static_cast<size_type>(end_ - data_) : 0;
        }
        size_type capacity() const noexcept
        {
            return data_ ? static_cast<size_type>(cap_ - data_) : 0;
        }
        size_type max_size() const noexcept
        {
            return (std::numeric_limits<size_type>::max)() / sizeof(T);
        }

        void reserve(size_type n)
        {
            if (n > max_size())
                detail::fatal("ct::Vector: capacidade invalida");
            if (n > capacity())
                change_capacity(n);
        }

        void shrink_to_fit()
        {
            if (data_ && end_ < cap_)
            {
                if (end_ == data_)
                {
                    this->deallocate(data_, capacity() * sizeof(T));
                    data_ = nullptr;
                    end_ = nullptr;
                    cap_ = nullptr;
                }
                else
                {
                    change_capacity(size());
                }
            }
        }

        void clear() noexcept
        {
            detail::destroy_n(data_, size(), trivial_dtor{});
            end_ = data_;
        }

        void push_back(const T &value)
        {
            if (CT_UNLIKELY(end_ == cap_))
            {
                if (contains_address(&value))
                {
                    T stable(value);
                    grow();
                    ::new (static_cast<void *>(end_)) T(detail::move(stable));
                    ++end_;
                    return;
                }
                grow();
            }
            ::new (static_cast<void *>(end_)) T(value);
            ++end_;
        }

        void push_back(T &&value)
        {
            if (CT_UNLIKELY(end_ == cap_))
            {
                if (contains_address(&value))
                {
                    T stable(detail::move(value));
                    grow();
                    ::new (static_cast<void *>(end_)) T(detail::move(stable));
                    ++end_;
                    return;
                }
                grow();
            }
            ::new (static_cast<void *>(end_)) T(detail::move(value));
            ++end_;
        }

        template <typename... Args>
        reference emplace_back(Args &&...args)
        {
            if (CT_UNLIKELY(end_ == cap_))
            {
                T stable(detail::forward<Args>(args)...);
                grow();
                T *p = ::new (static_cast<void *>(end_)) T(detail::move(stable));
                ++end_;
                return *p;
            }
            T *p = ::new (static_cast<void *>(end_)) T(detail::forward<Args>(args)...);
            ++end_;
            return *p;
        }

        void pop_back()
        {
            --end_;
            detail::destroy_n(end_, 1, trivial_dtor{});
        }

        void resize(size_type n)
        {
            const size_type current = size();
            if (n < current)
            {
                detail::destroy_n(data_ + n, current - n, trivial_dtor{});
                end_ = data_ + n;
                return;
            }
            if (n == current)
                return;
            reserve_for_growth(n);
            for (size_type i = current; i < n; ++i)
                ::new (static_cast<void *>(data_ + i)) T();
            end_ = data_ + n;
        }

        void resize(size_type n, const T &value)
        {
            size_type sz = size();
            if (n < sz)
            {
                detail::destroy_n(data_ + n, sz - n, trivial_dtor{});
                end_ = data_ + n;
            }
            else if (n > sz)
            {
                if (contains_address(&value) && n > capacity())
                {
                    T stable(value);
                    reserve_for_growth(n);
                    fill_new(sz, n, stable, trivial_copy{});
                }
                else
                {
                    reserve_for_growth(n);
                    fill_new(sz, n, value, trivial_copy{});
                }
                end_ = data_ + n;
            }
        }

        iterator insert(const_iterator pos, const T &value)
        {
            if (contains_address(&value))
            {
                T stable(value);
                return emplace(pos, detail::move(stable));
            }
            return emplace(pos, value);
        }

        iterator insert(const_iterator pos, T &&value)
        {
            if (contains_address(&value))
            {
                T stable(detail::move(value));
                return emplace(pos, detail::move(stable));
            }
            return emplace(pos, detail::move(value));
        }

        template <typename... Args>
        iterator emplace(const_iterator pos, Args &&...args)
        {
            size_type idx = data_ ? static_cast<size_type>(pos - data_) : 0;
            if (end_ != cap_ && idx == size())
            {
                T *p = ::new (static_cast<void *>(end_)) T(detail::forward<Args>(args)...);
                ++end_;
                return p;
            }
            T stable(detail::forward<Args>(args)...);
            if (CT_UNLIKELY(end_ == cap_))
                grow();
            if (idx < size())
            {

                open_gap(idx, trivial_copy{});
            }
            ::new (static_cast<void *>(data_ + idx)) T(detail::move(stable));
            ++end_;
            return data_ + idx;
        }

        iterator erase(const_iterator pos)
        {
            return erase(pos, pos + 1);
        }

        iterator erase(const_iterator first, const_iterator last)
        {
            size_type i = static_cast<size_type>(first - data_);
            size_type n = static_cast<size_type>(last - first);
            if (n)
            {
                detail::destroy_n(data_ + i, n, trivial_dtor{});
                close_gap(i, n, trivial_copy{});
                end_ -= n;
            }
            return i ? data_ + i : data_;
        }

        void swap(Vector &other) noexcept
        {
            detail::swap_vals(static_cast<Alloc &>(*this), static_cast<Alloc &>(other));
            detail::swap_vals(data_, other.data_);
            detail::swap_vals(end_, other.end_);
            detail::swap_vals(cap_, other.cap_);
        }

        const Alloc &get_allocator() const noexcept
        {
            return static_cast<const Alloc &>(*this);
        }

    private:
        T *data_; 
        T *end_;  
        T *cap_;  

        bool contains_address(const T *value) const noexcept
        {
            if (!data_ || !value)
                return false;
            const std::uintptr_t begin = reinterpret_cast<std::uintptr_t>(data_);
            const std::uintptr_t end = reinterpret_cast<std::uintptr_t>(end_);
            const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(value);
            return address >= begin && address < end;
        }

        void fill_new(size_type from, size_type to, const T &value, detail::true_type)
        {
            detail::fill_assign_n(data_ + from, to - from, value);
        }
        void fill_new(size_type from, size_type to, const T &value, detail::false_type)
        {
            for (size_type i = from; i < to; ++i)
                ::new (static_cast<void *>(data_ + i)) T(value);
        }

        void copy_construct_from(const T *src, size_type n)
        {
            detail::copy_construct_n(data_, src, n);
            end_ = n ? data_ + n : data_;
        }

        void open_gap(size_type idx, detail::true_type)
        {
            std::memmove(static_cast<void *>(data_ + idx + 1),
                         static_cast<const void *>(data_ + idx),
                         (size() - idx) * sizeof(T));
        }
        void open_gap(size_type idx, detail::false_type)
        {
            for (size_type i = size(); i > idx; --i)
            {
                ::new (static_cast<void *>(data_ + i)) T(detail::move(data_[i - 1]));
                data_[i - 1].~T();
            }
        }

        void close_gap(size_type idx, size_type n, detail::true_type)
        {
            std::memmove(static_cast<void *>(data_ + idx),
                         static_cast<const void *>(data_ + idx + n),
                         (size() - idx - n) * sizeof(T));
        }
        void close_gap(size_type idx, size_type n, detail::false_type)
        {
            for (size_type i = idx + n; i < size(); ++i)
            {
                ::new (static_cast<void *>(data_ + i - n)) T(detail::move(data_[i]));
                data_[i].~T();
            }
        }

        void grow()
        {
            size_type c = capacity();
            if (c > max_size() / 2)
                detail::fatal("ct::Vector: capacidade invalida");
            change_capacity(c ? c * 2 : 8);
        }

        void reserve_for_growth(size_type n)
        {
            size_type c = capacity();
            if (n > c)
            {
                if (c > max_size() / 2)
                    detail::fatal("ct::Vector: capacidade invalida");
                size_type target = c ? c * 2 : 8;
                if (target < n)
                    target = n;
                change_capacity(target);
            }
        }

        void change_capacity(size_type new_cap)
        {
            if (new_cap > max_size())
                detail::fatal("ct::Vector: capacidade invalida");
            reallocate_impl(new_cap, trivial_copy{});
        }
        void reallocate_impl(size_type new_cap, detail::true_type)
        {
            size_type sz = size();
            size_type old_bytes = 0;
            size_type new_bytes = 0;
            if (!detail::checked_mul(capacity(), sizeof(T), old_bytes) ||
                !detail::checked_mul(new_cap, sizeof(T), new_bytes))
                detail::fatal("ct::Vector: tamanho invalido");
            T *p = data_
                       ? static_cast<T *>(Alloc::reallocate(data_, old_bytes, new_bytes, alignof(T)))
                       : static_cast<T *>(this->allocate(new_bytes, alignof(T)));
            data_ = p;
            end_ = p + sz;
            cap_ = p + new_cap;
        }
        void reallocate_impl(size_type new_cap, detail::false_type)
        {
            size_type sz = size();
            size_type bytes = 0;
            if (!detail::checked_mul(new_cap, sizeof(T), bytes))
                detail::fatal("ct::Vector: tamanho invalido");
            T *p = static_cast<T *>(this->allocate(bytes, alignof(T)));
            detail::relocate_n(p, data_, sz, trivial_copy{});
            if (data_)
            {
                size_type old_bytes = 0;
                if (!detail::checked_mul(capacity(), sizeof(T), old_bytes))
                    detail::fatal("ct::Vector: tamanho invalido");
                this->deallocate(data_, old_bytes);
            }
            data_ = p;
            end_ = p + sz;
            cap_ = p + new_cap;
        }
    };

    template <typename T, typename A1, typename A2>
    inline bool operator==(const Vector<T, A1> &a, const Vector<T, A2> &b)
    {
        if (a.size() != b.size())
            return false;
        for (typename Vector<T, A1>::size_type i = 0; i < a.size(); ++i)
            if (!(a[i] == b[i]))
                return false;
        return true;
    }

    template <typename T, typename A1, typename A2>
    inline bool operator!=(const Vector<T, A1> &a, const Vector<T, A2> &b)
    {
        return !(a == b);
    }

    template <typename T, typename A>
    inline void swap(Vector<T, A> &a, Vector<T, A> &b) noexcept
    {
        a.swap(b);
    }

}
