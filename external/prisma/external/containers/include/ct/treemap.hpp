
#pragma once

#include "detail/utils.hpp"
#include "pool.hpp"

namespace ct
{

    template <typename K, typename V, typename L = Less<K>>
    class TreeMap : private L
    {
    public:
        struct Entry
        {
            K key;
            V value;

        private:
            friend class TreeMap;
            Entry *l;
            Entry *r;
            Entry *p;
            bool red;
        };

        using size_type = std::size_t;

        TreeMap() : pool_(), size_(0) { init_nil(); }
        explicit TreeMap(const L &less) : L(less), pool_(), size_(0) { init_nil(); }

        ~TreeMap() { destroy_subtree(root_); }

        TreeMap(const TreeMap &o) : L(static_cast<const L &>(o)), pool_(), size_(0)
        {
            init_nil();
            for (const Entry *e = o.leftmost(); e != o.nil_; e = o.successor(e))
                put(e->key, e->value);
        }

        TreeMap(TreeMap &&o) noexcept
            : L(static_cast<L &&>(o)), pool_(detail::move(o.pool_)),
              nil_(o.nil_), root_(o.root_), size_(o.size_)
        {
            o.init_nil(); 
            o.size_ = 0;
        }

        TreeMap &operator=(const TreeMap &o)
        {
            if (this != &o)
            {
                clear();
                for (const Entry *e = o.leftmost(); e != o.nil_; e = o.successor(e))
                    put(e->key, e->value);
            }
            return *this;
        }

        TreeMap &operator=(TreeMap &&o) noexcept
        {
            if (this != &o)
            {
                destroy_subtree(root_);
                pool_ = detail::move(o.pool_);
                nil_ = o.nil_;
                root_ = o.root_;
                size_ = o.size_;
                o.init_nil();
                o.size_ = 0;
            }
            return *this;
        }

        size_type size() const noexcept { return size_; }
        bool empty() const noexcept { return size_ == 0; }

        V *find(const K &k) noexcept
        {
            Entry *x = root_;
            while (x != nil_)
            {
                if (less(k, x->key))
                    x = left(x);
                else if (less(x->key, k))
                    x = right(x);
                else
                    return &x->value;
            }
            return nullptr;
        }
        const V *find(const K &k) const noexcept
        {
            return const_cast<TreeMap *>(this)->find(k);
        }

        bool contains(const K &k) const noexcept { return find(k) != nullptr; }

        const V &get(const K &k, const V &fallback) const noexcept
        {
            const V *v = find(k);
            return v ? *v : fallback;
        }

        template <typename KK, typename VV>
        V &put(KK &&k, VV &&v)
        {
            Entry *y = nil_;
            Entry *x = root_;
            while (x != nil_)
            {
                y = x;
                if (less(k, x->key))
                    x = left(x);
                else if (less(x->key, k))
                    x = right(x);
                else
                {
                    x->value = detail::forward<VV>(v);
                    return x->value;
                }
            }
            Entry *z = pool_.allocate();
            ::new (static_cast<void *>(&z->key)) K(detail::forward<KK>(k));
            ::new (static_cast<void *>(&z->value)) V(detail::forward<VV>(v));
            link(z, y);
            return z->value;
        }

        V &operator[](const K &k)
        {
            V *v = find(k);
            if (v)
                return *v;
            return put(k, V());
        }

        bool erase(const K &k)
        {
            Entry *z = root_;
            while (z != nil_)
            {
                if (less(k, z->key))
                    z = left(z);
                else if (less(z->key, k))
                    z = right(z);
                else
                    break;
            }
            if (z == nil_)
                return false;
            erase_node(z);
            return true;
        }

        void clear()
        {
            destroy_subtree(root_);
            root_ = nil_;
            size_ = 0;
        }

        class iterator
        {
            Entry *e_;
            const TreeMap *m_;

        public:
            iterator(Entry *e, const TreeMap *m) : e_(e), m_(m) {}
            Entry &operator*() const { return *e_; }
            Entry *operator->() const { return e_; }
            iterator &operator++()
            {
                e_ = const_cast<Entry *>(m_->successor(e_));
                return *this;
            }
            bool operator==(const iterator &o) const { return e_ == o.e_; }
            bool operator!=(const iterator &o) const { return e_ != o.e_; }
        };

        iterator begin() noexcept { return iterator(const_cast<Entry *>(leftmost()), this); }
        iterator end() noexcept { return iterator(nil_, this); }

        iterator lower_bound_it(const K &k) noexcept
        {
            Entry *best = nil_;
            Entry *x = root_;
            while (x != nil_)
            {
                if (!less(x->key, k))
                {
                    best = x;
                    x = left(x);
                }
                else
                    x = right(x);
            }
            return iterator(best, this);
        }

        bool validate() const
        {
            if (root_ != nil_ && is_red(root_))
                return false;
            return black_height(root_) >= 0;
        }

    private:
        Pool<Entry> pool_;
        Entry *nil_;  
        Entry *root_;
        size_type size_;

        static Entry *&left_ref(Entry *e) { return e->l; }
        static Entry *&right_ref(Entry *e) { return e->r; }
        static Entry *&parent_ref(Entry *e) { return e->p; }
        static Entry *left(const Entry *e) { return e->l; }
        static Entry *right(const Entry *e) { return e->r; }
        static Entry *parent(const Entry *e) { return e->p; }
        static bool is_red(const Entry *e) { return e->red; }
        static void set_red(Entry *e, bool r) { e->red = r; }

        bool less(const K &a, const K &b) const
        {
            return (*static_cast<const L *>(this))(a, b);
        }

        void init_nil()
        {
            nil_ = pool_.allocate(); 
            left_ref(nil_) = nil_;
            right_ref(nil_) = nil_;
            parent_ref(nil_) = nil_;
            set_red(nil_, false);
            root_ = nil_;
        }

        const Entry *leftmost() const
        {
            const Entry *x = root_;
            if (x == nil_)
                return nil_;
            while (left(x) != nil_)
                x = left(x);
            return x;
        }

        const Entry *successor(const Entry *x) const
        {
            if (right(x) != nil_)
            {
                x = right(x);
                while (left(x) != nil_)
                    x = left(x);
                return x;
            }
            const Entry *y = parent(x);
            while (y != nil_ && x == right(y))
            {
                x = y;
                y = parent(y);
            }
            return y;
        }

        void rotate_left(Entry *x)
        {
            Entry *y = right(x);
            right_ref(x) = left(y);
            if (left(y) != nil_)
                parent_ref(left(y)) = x;
            parent_ref(y) = parent(x);
            if (parent(x) == nil_)
                root_ = y;
            else if (x == left(parent(x)))
                left_ref(parent(x)) = y;
            else
                right_ref(parent(x)) = y;
            left_ref(y) = x;
            parent_ref(x) = y;
        }

        void rotate_right(Entry *x)
        {
            Entry *y = left(x);
            left_ref(x) = right(y);
            if (right(y) != nil_)
                parent_ref(right(y)) = x;
            parent_ref(y) = parent(x);
            if (parent(x) == nil_)
                root_ = y;
            else if (x == right(parent(x)))
                right_ref(parent(x)) = y;
            else
                left_ref(parent(x)) = y;
            right_ref(y) = x;
            parent_ref(x) = y;
        }

        void link(Entry *z, Entry *y)
        {
            parent_ref(z) = y;
            if (y == nil_)
                root_ = z;
            else if (less(z->key, y->key))
                left_ref(y) = z;
            else
                right_ref(y) = z;
            left_ref(z) = nil_;
            right_ref(z) = nil_;
            set_red(z, true);
            insert_fixup(z);
            ++size_;
        }

        void insert_fixup(Entry *z)
        {
            while (is_red(parent(z)))
            {
                Entry *g = parent(parent(z));
                if (parent(z) == left(g))
                {
                    Entry *u = right(g);
                    if (is_red(u))
                    {
                        set_red(parent(z), false);
                        set_red(u, false);
                        set_red(g, true);
                        z = g;
                    }
                    else
                    {
                        if (z == right(parent(z)))
                        {
                            z = parent(z);
                            rotate_left(z);
                        }
                        set_red(parent(z), false);
                        set_red(parent(parent(z)), true);
                        rotate_right(parent(parent(z)));
                    }
                }
                else
                {
                    Entry *u = left(g);
                    if (is_red(u))
                    {
                        set_red(parent(z), false);
                        set_red(u, false);
                        set_red(g, true);
                        z = g;
                    }
                    else
                    {
                        if (z == left(parent(z)))
                        {
                            z = parent(z);
                            rotate_right(z);
                        }
                        set_red(parent(z), false);
                        set_red(parent(parent(z)), true);
                        rotate_left(parent(parent(z)));
                    }
                }
            }
            set_red(root_, false);
        }

        void transplant(Entry *u, Entry *v)
        {
            if (parent(u) == nil_)
                root_ = v;
            else if (u == left(parent(u)))
                left_ref(parent(u)) = v;
            else
                right_ref(parent(u)) = v;
            parent_ref(v) = parent(u);
        }

        void erase_node(Entry *z)
        {
            Entry *y = z;
            Entry *x;
            bool y_was_red = is_red(y);
            if (left(z) == nil_)
            {
                x = right(z);
                transplant(z, right(z));
            }
            else if (right(z) == nil_)
            {
                x = left(z);
                transplant(z, left(z));
            }
            else
            {
                y = right(z);
                while (left(y) != nil_)
                    y = left(y);
                y_was_red = is_red(y);
                x = right(y);
                if (parent(y) == z)
                {
                    parent_ref(x) = y; 
                }
                else
                {
                    transplant(y, right(y));
                    right_ref(y) = right(z);
                    parent_ref(right(y)) = y;
                }
                transplant(z, y);
                left_ref(y) = left(z);
                parent_ref(left(y)) = y;
                set_red(y, is_red(z));
            }
            if (!y_was_red)
                erase_fixup(x);
            z->key.~K();
            z->value.~V();
            pool_.deallocate(z);
            --size_;
        }

        void erase_fixup(Entry *x)
        {
            while (x != root_ && !is_red(x))
            {
                if (x == left(parent(x)))
                {
                    Entry *w = right(parent(x));
                    if (is_red(w))
                    {
                        set_red(w, false);
                        set_red(parent(x), true);
                        rotate_left(parent(x));
                        w = right(parent(x));
                    }
                    if (!is_red(left(w)) && !is_red(right(w)))
                    {
                        set_red(w, true);
                        x = parent(x);
                    }
                    else
                    {
                        if (!is_red(right(w)))
                        {
                            set_red(left(w), false);
                            set_red(w, true);
                            rotate_right(w);
                            w = right(parent(x));
                        }
                        set_red(w, is_red(parent(x)));
                        set_red(parent(x), false);
                        set_red(right(w), false);
                        rotate_left(parent(x));
                        x = root_;
                    }
                }
                else
                {
                    Entry *w = left(parent(x));
                    if (is_red(w))
                    {
                        set_red(w, false);
                        set_red(parent(x), true);
                        rotate_right(parent(x));
                        w = left(parent(x));
                    }
                    if (!is_red(right(w)) && !is_red(left(w)))
                    {
                        set_red(w, true);
                        x = parent(x);
                    }
                    else
                    {
                        if (!is_red(left(w)))
                        {
                            set_red(right(w), false);
                            set_red(w, true);
                            rotate_left(w);
                            w = left(parent(x));
                        }
                        set_red(w, is_red(parent(x)));
                        set_red(parent(x), false);
                        set_red(left(w), false);
                        rotate_right(parent(x));
                        x = root_;
                    }
                }
            }
            set_red(x, false);
        }

        void destroy_subtree(Entry *n)
        {
            if (n == nil_ || n == nullptr)
                return;
            destroy_subtree(left(n));
            destroy_subtree(right(n));
            n->key.~K();
            n->value.~V();
            pool_.deallocate(n);
        }

        int black_height(const Entry *n) const
        {
            if (n == nil_)
                return 0;
            if (is_red(n) && (is_red(left(n)) || is_red(right(n))))
                return -1; 
            if (left(n) != nil_ && !less(left(n)->key, n->key))
                return -1; 
            if (right(n) != nil_ && !less(n->key, right(n)->key))
                return -1;
            int hl = black_height(left(n));
            int hr = black_height(right(n));
            if (hl < 0 || hr < 0 || hl != hr)
                return -1;
            return hl + (is_red(n) ? 0 : 1);
        }
    };

} 