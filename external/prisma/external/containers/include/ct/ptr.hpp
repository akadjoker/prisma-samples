#pragma once

#include "detail/utils.hpp"

namespace ct
{

    template <typename T>
    class Unique
    {
    public:
        using element_type = T;

        Unique() noexcept : p_(nullptr) {}
        Unique(std::nullptr_t) noexcept : p_(nullptr) {}

        static Unique adopt(T *p) noexcept
        {
            Unique u;
            u.p_ = p;
            return u;
        }

        Unique(const Unique &) = delete;
        Unique &operator=(const Unique &) = delete;

        Unique(Unique &&o) noexcept : p_(o.p_) { o.p_ = nullptr; }

        template <typename D,
                  typename = typename detail::enable_if<
                      std::is_convertible<D *, T *>::value>::type>
        Unique(Unique<D> &&o) noexcept : p_(nullptr)
        {
            static_assert(std::is_same<D, T>::value || std::has_virtual_destructor<T>::value,
                          "ct::Unique: upcast exige destrutor virtual na base");
            D *d = o.release();
            p_ = d;
            if (static_cast<const void *>(p_) != static_cast<const void *>(d))
                detail::fatal("ct::Unique: upcast com ajuste de ponteiro (heranca multipla) nao e suportado");
        }

        Unique &operator=(Unique &&o) noexcept
        {
            if (this != &o)
            {
                reset();
                p_ = o.p_;
                o.p_ = nullptr;
            }
            return *this;
        }

        ~Unique() { reset(); }

        void reset() noexcept
        {
            if (!p_)
                return;
            T *p = p_;
            p_ = nullptr; 
            p->~T();
            HeapAlloc().deallocate(static_cast<void *>(p), sizeof(T));
        }

        T *release() noexcept
        {
            T *p = p_;
            p_ = nullptr;
            return p;
        }

        void swap(Unique &o) noexcept
        {
            T *t = p_;
            p_ = o.p_;
            o.p_ = t;
        }

        T *get() const noexcept { return p_; }
        explicit operator bool() const noexcept { return p_ != nullptr; }

        T &operator*() const
        {
            if (CT_UNLIKELY(!p_))
                detail::fatal("ct::Unique: dereferenciar um ponteiro vazio");
            return *p_;
        }

        T *operator->() const
        {
            if (CT_UNLIKELY(!p_))
                detail::fatal("ct::Unique: dereferenciar um ponteiro vazio");
            return p_;
        }

    private:
        T *p_;
    };

    template <typename T, typename... Args>
    inline Unique<T> make_unique(Args &&...args)
    {
        HeapAlloc a;
        void *raw = a.allocate(sizeof(T), alignof(T));
        return Unique<T>::adopt(::new (raw) T(detail::forward<Args>(args)...));
    }

    template <typename T>
    inline bool operator==(const Unique<T> &a, std::nullptr_t) noexcept
    {
        return a.get() == nullptr;
    }
    template <typename T>
    inline bool operator!=(const Unique<T> &a, std::nullptr_t) noexcept
    {
        return a.get() != nullptr;
    }

    namespace detail
    {
        struct RcCtrl
        {
            std::uint32_t strong;
            std::uint32_t weak; 
            void (*op)(RcCtrl *, int);
        };

        enum
        {
            kRcDestroy = 0,
            kRcFree = 1
        };

        template <typename T>
        struct RcBlock
        {
            enum : std::size_t
            {
                align = alignof(T) > alignof(RcCtrl) ? alignof(T) : alignof(RcCtrl),
                offset = ((sizeof(RcCtrl) + align - 1) / align) * align,
                total = offset + sizeof(T)
            };
        };

        template <typename T>
        inline void rc_op(RcCtrl *c, int what)
        {
            char *base = reinterpret_cast<char *>(c);
            if (what == kRcDestroy)
                reinterpret_cast<T *>(base + RcBlock<T>::offset)->~T();
            else
                HeapAlloc().deallocate(static_cast<void *>(base), RcBlock<T>::total);
        }
    } 

    template <typename T>
    class Weak;
    template <typename T>
    class Rc;
    template <typename T, typename... Args>
    Rc<T> make_rc(Args &&...args);

    template <typename T>
    class Rc
    {
    public:
        using element_type = T;

        Rc() noexcept : p_(nullptr), c_(nullptr) {}
        Rc(std::nullptr_t) noexcept : p_(nullptr), c_(nullptr) {}

        Rc(const Rc &o) noexcept : p_(o.p_), c_(o.c_)
        {
            if (c_)
                ++c_->strong;
        }

        Rc(Rc &&o) noexcept : p_(o.p_), c_(o.c_)
        {
            o.p_ = nullptr;
            o.c_ = nullptr;
        }

        template <typename D,
                  typename = typename detail::enable_if<
                      std::is_convertible<D *, T *>::value>::type>
        Rc(const Rc<D> &o) noexcept : p_(o.p_), c_(o.c_)
        {
            if (c_)
                ++c_->strong;
        }

        Rc &operator=(const Rc &o) noexcept
        {
            if (c_ != o.c_ || p_ != o.p_)
            {
                Rc tmp(o); 
                swap(tmp);
            }
            return *this;
        }

        Rc &operator=(Rc &&o) noexcept
        {
            if (this != &o)
            {
                Rc tmp(detail::move(o));
                swap(tmp);
            }
            return *this;
        }

        ~Rc() { release(); }

        void reset() noexcept
        {
            release();
            p_ = nullptr;
            c_ = nullptr;
        }

        void swap(Rc &o) noexcept
        {
            T *p = p_;
            detail::RcCtrl *c = c_;
            p_ = o.p_;
            c_ = o.c_;
            o.p_ = p;
            o.c_ = c;
        }

        T *get() const noexcept { return p_; }
        std::uint32_t use_count() const noexcept { return c_ ? c_->strong : 0; }
        bool unique() const noexcept { return c_ && c_->strong == 1; }
        explicit operator bool() const noexcept { return p_ != nullptr; }

        T &operator*() const
        {
            if (CT_UNLIKELY(!p_))
                detail::fatal("ct::Rc: dereferenciar um ponteiro vazio");
            return *p_;
        }

        T *operator->() const
        {
            if (CT_UNLIKELY(!p_))
                detail::fatal("ct::Rc: dereferenciar um ponteiro vazio");
            return p_;
        }

    private:

        Rc(T *p, detail::RcCtrl *c) noexcept : p_(p), c_(c) {}
        detail::RcCtrl *ctrl() const noexcept { return c_; }

        void release() noexcept
        {
            if (!c_)
                return;
            if (--c_->strong == 0)
            {
                c_->op(c_, detail::kRcDestroy);
                if (--c_->weak == 0)
                    c_->op(c_, detail::kRcFree);
            }
        }

        T *p_;
        detail::RcCtrl *c_;

        template <typename U>
        friend class Weak;
        template <typename U>
        friend class Rc;
        template <typename U, typename... Args>
        friend Rc<U> make_rc(Args &&...args);
    };

    template <typename T>
    class Weak
    {
    public:
        Weak() noexcept : p_(nullptr), c_(nullptr) {}

        Weak(const Rc<T> &r) noexcept : p_(r.p_), c_(r.c_)
        {
            if (c_)
                ++c_->weak;
        }

        Weak(const Weak &o) noexcept : p_(o.p_), c_(o.c_)
        {
            if (c_)
                ++c_->weak;
        }

        Weak(Weak &&o) noexcept : p_(o.p_), c_(o.c_)
        {
            o.p_ = nullptr;
            o.c_ = nullptr;
        }

        Weak &operator=(const Weak &o) noexcept
        {
            if (this != &o)
            {
                Weak tmp(o);
                swap(tmp);
            }
            return *this;
        }

        Weak &operator=(Weak &&o) noexcept
        {
            if (this != &o)
            {
                Weak tmp(detail::move(o));
                swap(tmp);
            }
            return *this;
        }

        Weak &operator=(const Rc<T> &r) noexcept
        {
            Weak tmp(r);
            swap(tmp);
            return *this;
        }

        ~Weak() { release(); }

        void reset() noexcept
        {
            release();
            p_ = nullptr;
            c_ = nullptr;
        }

        void swap(Weak &o) noexcept
        {
            T *p = p_;
            detail::RcCtrl *c = c_;
            p_ = o.p_;
            c_ = o.c_;
            o.p_ = p;
            o.c_ = c;
        }

        bool expired() const noexcept { return !c_ || c_->strong == 0; }
        std::uint32_t use_count() const noexcept { return c_ ? c_->strong : 0; }

        Rc<T> lock() const noexcept
        {
            if (!c_ || c_->strong == 0)
                return Rc<T>();
            ++c_->strong;
            return Rc<T>(p_, c_);
        }

    private:
        void release() noexcept
        {
            if (c_ && --c_->weak == 0)
                c_->op(c_, detail::kRcFree);
        }

        T *p_;
        detail::RcCtrl *c_;
    };

    template <typename T, typename... Args>
    inline Rc<T> make_rc(Args &&...args)
    {
        HeapAlloc a;
        char *base = static_cast<char *>(
            a.allocate(detail::RcBlock<T>::total, detail::RcBlock<T>::align));
        detail::RcCtrl *c = reinterpret_cast<detail::RcCtrl *>(base);
        c->strong = 1;
        c->weak = 1;
        c->op = &detail::rc_op<T>;
        T *p = ::new (static_cast<void *>(base + detail::RcBlock<T>::offset))
            T(detail::forward<Args>(args)...);
        return Rc<T>(p, c);
    }

    template <typename T>
    inline bool operator==(const Rc<T> &a, const Rc<T> &b) noexcept
    {
        return a.get() == b.get();
    }
    template <typename T>
    inline bool operator!=(const Rc<T> &a, const Rc<T> &b) noexcept
    {
        return a.get() != b.get();
    }
    template <typename T>
    inline bool operator==(const Rc<T> &a, std::nullptr_t) noexcept
    {
        return a.get() == nullptr;
    }
    template <typename T>
    inline bool operator!=(const Rc<T> &a, std::nullptr_t) noexcept
    {
        return a.get() != nullptr;
    }

} 