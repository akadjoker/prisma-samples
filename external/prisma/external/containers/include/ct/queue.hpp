
#pragma once

#include "deque.hpp"

namespace ct
{

    template <typename T, typename Container = Deque<T>>
    class Queue
    {
    public:
        using container_type = Container;
        using value_type = typename Container::value_type;
        using size_type = typename Container::size_type;
        using reference = typename Container::reference;
        using const_reference = typename Container::const_reference;

        Queue() = default;

        explicit Queue(const Container &c) : c_(c) {}
        explicit Queue(Container &&c) : c_(detail::move(c)) {}

        bool empty() const noexcept { return c_.empty(); }
        size_type size() const noexcept { return c_.size(); }
        size_type capacity() const noexcept { return c_.capacity(); }
        void reserve(size_type n) { c_.reserve(n); }

        reference front() { return c_.front(); }
        const_reference front() const { return c_.front(); }
        reference back() { return c_.back(); }
        const_reference back() const { return c_.back(); }

        void push(const T &value) { c_.push_back(value); }
        void push(T &&value) { c_.push_back(detail::move(value)); }

        template <typename... Args>
        reference emplace(Args &&...args)
        {
            return c_.emplace_back(detail::forward<Args>(args)...);
        }

        void pop() { c_.pop_front(); }

        void clear() noexcept { c_.clear(); }

        void swap(Queue &other) noexcept { c_.swap(other.c_); }

        const Container &container() const noexcept { return c_; }

        template <typename U, typename C>
        friend bool operator==(const Queue<U, C> &a, const Queue<U, C> &b);

    private:
        Container c_;
    };

    template <typename T, typename C>
    inline bool operator==(const Queue<T, C> &a, const Queue<T, C> &b)
    {
        return a.c_ == b.c_;
    }

    template <typename T, typename C>
    inline bool operator!=(const Queue<T, C> &a, const Queue<T, C> &b)
    {
        return !(a == b);
    }

    template <typename T, typename C>
    inline void swap(Queue<T, C> &a, Queue<T, C> &b) noexcept
    {
        a.swap(b);
    }

} 