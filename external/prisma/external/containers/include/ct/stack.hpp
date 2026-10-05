
#pragma once

#include "vector.hpp"

namespace ct
{

    template <typename T, typename Container = Vector<T>>
    class Stack
    {
    public:
        using container_type = Container;
        using value_type = typename Container::value_type;
        using size_type = typename Container::size_type;
        using reference = typename Container::reference;
        using const_reference = typename Container::const_reference;

        Stack() = default;

        explicit Stack(const Container &c) : c_(c) {}
        explicit Stack(Container &&c) : c_(detail::move(c)) {}

        bool empty() const noexcept { return c_.empty(); }
        size_type size() const noexcept { return c_.size(); }
        size_type capacity() const noexcept { return c_.capacity(); }
        void reserve(size_type n) { c_.reserve(n); }

        reference top() { return c_.back(); }
        const_reference top() const { return c_.back(); }

        void push(const T &value) { c_.push_back(value); }
        void push(T &&value) { c_.push_back(detail::move(value)); }

        template <typename... Args>
        reference emplace(Args &&...args)
        {
            return c_.emplace_back(detail::forward<Args>(args)...);
        }

        void pop() { c_.pop_back(); }

        void clear() noexcept { c_.clear(); }

        void swap(Stack &other) noexcept { c_.swap(other.c_); }

        const Container &container() const noexcept { return c_; }

        template <typename U, typename C>
        friend bool operator==(const Stack<U, C> &a, const Stack<U, C> &b);

    private:
        Container c_;
    };

    template <typename T, typename C>
    inline bool operator==(const Stack<T, C> &a, const Stack<T, C> &b)
    {
        return a.c_ == b.c_;
    }

    template <typename T, typename C>
    inline bool operator!=(const Stack<T, C> &a, const Stack<T, C> &b)
    {
        return !(a == b);
    }

    template <typename T, typename C>
    inline void swap(Stack<T, C> &a, Stack<T, C> &b) noexcept
    {
        a.swap(b);
    }

} 