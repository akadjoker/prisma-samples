#pragma once

#include "vector.hpp"

namespace ct
{
    template <typename T, typename Compare = Less<T>, typename Container = Vector<T>>
    class PriorityQueue
    {
    public:
        using value_type = typename Container::value_type;
        using size_type = typename Container::size_type;
        using reference = typename Container::reference;
        using const_reference = typename Container::const_reference;

        PriorityQueue() = default;
        explicit PriorityQueue(const Compare &compare) : compare_(compare) {}
        explicit PriorityQueue(const Container &container, const Compare &compare = Compare())
            : compare_(compare), container_(container) { makeHeap(); }
        explicit PriorityQueue(Container &&container, const Compare &compare = Compare())
            : compare_(compare), container_(detail::move(container)) { makeHeap(); }

        bool empty() const noexcept { return container_.empty(); }
        size_type size() const noexcept { return container_.size(); }
        size_type capacity() const noexcept { return container_.capacity(); }
        void reserve(size_type count) { container_.reserve(count); }

        const_reference top() const { return container_.front(); }

        void push(const T &value)
        {
            container_.push_back(value);
            siftUp(container_.size() - 1);
        }

        void push(T &&value)
        {
            container_.push_back(detail::move(value));
            siftUp(container_.size() - 1);
        }

        template <typename... Args>
        void emplace(Args &&...args)
        {
            container_.emplace_back(detail::forward<Args>(args)...);
            siftUp(container_.size() - 1);
        }

        void pop()
        {
            const size_type count = container_.size();
            if (count < 2) { container_.clear(); return; }
            detail::swap_vals(container_[0], container_[count - 1]);
            container_.pop_back();
            siftDown(0);
        }

        void clear() noexcept { container_.clear(); }
        void swap(PriorityQueue &other) noexcept
        {
            container_.swap(other.container_);
            detail::swap_vals(compare_, other.compare_);
        }

        const Container &container() const noexcept { return container_; }

    private:
        Compare compare_;
        Container container_;

        void makeHeap()
        {
            for (size_type index = container_.size() / 2; index > 0; --index)
                siftDown(index - 1);
        }

        void siftUp(size_type index)
        {
            while (index)
            {
                const size_type parent = (index - 1) / 2;
                if (!compare_(container_[parent], container_[index])) return;
                detail::swap_vals(container_[parent], container_[index]);
                index = parent;
            }
        }

        void siftDown(size_type index)
        {
            const size_type count = container_.size();
            for (;;)
            {
                size_type child = index * 2 + 1;
                if (child >= count) return;
                if (child + 1 < count && compare_(container_[child], container_[child + 1])) ++child;
                if (!compare_(container_[index], container_[child])) return;
                detail::swap_vals(container_[index], container_[child]);
                index = child;
            }
        }
    };

    template <typename T, typename Compare, typename Container>
    inline void swap(PriorityQueue<T, Compare, Container> &a, PriorityQueue<T, Compare, Container> &b) noexcept
    {
        a.swap(b);
    }
}
