#pragma once

#include "detail/utils.hpp"

namespace ct
{
    template <typename Sig>
    class Function; 

    template <typename R, typename... Args>
    class Function<R(Args...)>
    {
    public:

        static constexpr std::size_t kSboSize = 3 * sizeof(void *);
        static constexpr std::size_t kSboAlign = alignof(std::max_align_t);

        Function() noexcept : heap_(nullptr), on_heap_(false), invoke_(nullptr), manage_(nullptr) {}
        Function(std::nullptr_t) noexcept : Function() {}

        template <typename F,
                  typename detail::enable_if<
                      !detail::is_same<typename std::decay<F>::type, Function>::value, int>::type = 0>
        Function(F f) : heap_(nullptr), on_heap_(false), invoke_(nullptr), manage_(nullptr)
        {
            construct(detail::move(f));
        }

        Function(const Function &o) : heap_(nullptr), on_heap_(false), invoke_(o.invoke_), manage_(o.manage_)
        {
            if (manage_)
                manage_(Op::Clone, const_cast<Function *>(&o), this);
        }
        Function(Function &&o) noexcept
            : heap_(nullptr), on_heap_(false), invoke_(o.invoke_), manage_(o.manage_)
        {
            if (manage_)
                manage_(Op::MoveInto, &o, this);
        }
        Function &operator=(const Function &o)
        {
            if (this != &o)
            {
                Function tmp(o);
                swap(tmp);
            }
            return *this;
        }
        Function &operator=(Function &&o) noexcept
        {
            if (this != &o)
            {
                reset();
                if (o.manage_)
                    o.manage_(Op::MoveInto, &o, this);
            }
            return *this;
        }
        Function &operator=(std::nullptr_t) noexcept
        {
            reset();
            return *this;
        }
        ~Function() { reset(); }

        R operator()(Args... args) const
        {
            if (!invoke_)
                detail::fatal("ct::Function: chamada com alvo vazio");
            return invoke_(const_cast<void *>(storage()), detail::forward<Args>(args)...);
        }

        explicit operator bool() const noexcept { return invoke_ != nullptr; }

        void reset() noexcept
        {
            if (manage_)
                manage_(Op::Destroy, this, nullptr);
            if (on_heap_ && heap_)
                HeapAlloc().deallocate(heap_, 0);
            heap_ = nullptr;
            on_heap_ = false;
            invoke_ = nullptr;
            manage_ = nullptr;
        }

        void swap(Function &o) noexcept
        {
            if (this == &o)
                return;
            Function tmp(detail::move(*this));
            *this = detail::move(o);
            o = detail::move(tmp);
        }

    private:
        enum class Op
        {
            Destroy, 
            Clone,   
            MoveInto 
        };
        using ManageFn = void (*)(Op, Function *self, Function *other);
        using InvokeFn = R (*)(void *, Args...);

        alignas(kSboAlign) unsigned char buf_[kSboSize];
        void *heap_;
        bool on_heap_;
        InvokeFn invoke_;
        ManageFn manage_;

        void *storage_mut() noexcept { return on_heap_ ? heap_ : static_cast<void *>(buf_); }
        const void *storage() const noexcept
        {
            return on_heap_ ? heap_ : static_cast<const void *>(buf_);
        }

        template <typename F>
        using FitsTag = detail::integral_constant<
            bool, (sizeof(F) <= kSboSize && alignof(F) <= kSboAlign)>;

        template <typename F>
        void construct(F &&f)
        {
            using D = typename std::decay<F>::type;
            construct_impl(detail::forward<F>(f), FitsTag<D>{});
            invoke_ = &invoke_thunk<D>;
            manage_ = &manage_thunk<D>;
        }
        template <typename F>
        void construct_impl(F &&f, detail::true_type )
        {
            using D = typename std::decay<F>::type;
            ::new (static_cast<void *>(buf_)) D(detail::forward<F>(f));
            on_heap_ = false;
        }
        template <typename F>
        void construct_impl(F &&f, detail::false_type )
        {
            using D = typename std::decay<F>::type;
            HeapAlloc a;
            heap_ = a.allocate(sizeof(D), alignof(D));
            ::new (heap_) D(detail::forward<F>(f));
            on_heap_ = true;
        }

        template <typename F>
        static R invoke_thunk(void *obj, Args... args)
        {
            return (*static_cast<F *>(obj))(detail::forward<Args>(args)...);
        }

        template <typename F>
        static void clone_into(const F &src, Function *other, detail::true_type)
        {
            ::new (static_cast<void *>(other->buf_)) F(src);
            other->on_heap_ = false;
        }
        template <typename F>
        static void clone_into(const F &src, Function *other, detail::false_type)
        {
            HeapAlloc a;
            other->heap_ = a.allocate(sizeof(F), alignof(F));
            ::new (other->heap_) F(src);
            other->on_heap_ = true;
        }

        template <typename F>
        static void move_into(Function *self, Function *other, detail::true_type )
        {
            ::new (static_cast<void *>(other->buf_))
                F(detail::move(*static_cast<F *>(self->storage_mut())));
            static_cast<F *>(self->storage_mut())->~F();
            other->on_heap_ = false;
        }
        template <typename F>
        static void move_into(Function *self, Function *other, detail::false_type )
        {
            other->heap_ = self->heap_; 
            other->on_heap_ = true;
            self->heap_ = nullptr;
        }

        template <typename F>
        static void manage_thunk(Op op, Function *self, Function *other)
        {
            switch (op)
            {
            case Op::Destroy:
                static_cast<F *>(self->storage_mut())->~F();
                break;
            case Op::Clone:
                clone_into(*static_cast<const F *>(self->storage()), other, FitsTag<F>{});
                other->invoke_ = self->invoke_;
                other->manage_ = self->manage_;
                break;
            case Op::MoveInto:
                move_into<F>(self, other, FitsTag<F>{});
                other->invoke_ = self->invoke_;
                other->manage_ = self->manage_;
                self->invoke_ = nullptr;
                self->manage_ = nullptr;
                self->on_heap_ = false;
                break;
            }
        }
    };

    template <typename R, typename... Args>
    inline bool operator==(const Function<R(Args...)> &f, std::nullptr_t) noexcept
    {
        return !f;
    }
    template <typename R, typename... Args>
    inline bool operator==(std::nullptr_t, const Function<R(Args...)> &f) noexcept
    {
        return !f;
    }
    template <typename R, typename... Args>
    inline bool operator!=(const Function<R(Args...)> &f, std::nullptr_t) noexcept
    {
        return static_cast<bool>(f);
    }
    template <typename R, typename... Args>
    inline bool operator!=(std::nullptr_t, const Function<R(Args...)> &f) noexcept
    {
        return static_cast<bool>(f);
    }

    template <typename R, typename... Args>
    inline void swap(Function<R(Args...)> &a, Function<R(Args...)> &b) noexcept
    {
        a.swap(b);
    }

} 