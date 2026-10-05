#pragma once

#include "detail/utils.hpp"

namespace ct
{
    namespace detail
    {
        constexpr std::size_t variant_max(std::size_t a, std::size_t b) { return a > b ? a : b; }

        template <bool B, typename T, typename F>
        struct conditional
        {
            using type = F;
        };
        template <typename T, typename F>
        struct conditional<true, T, F>
        {
            using type = T;
        };

        template <typename... Ts>
        struct VariantMaxSize; 
        template <typename T>
        struct VariantMaxSize<T>
        {
            static constexpr std::size_t value = sizeof(T);
        };
        template <typename T, typename... Rest>
        struct VariantMaxSize<T, Rest...>
        {
            static constexpr std::size_t value = variant_max(sizeof(T), VariantMaxSize<Rest...>::value);
        };

        template <typename... Ts>
        struct VariantMaxAlign; 
        template <typename T>
        struct VariantMaxAlign<T>
        {
            static constexpr std::size_t value = alignof(T);
        };
        template <typename T, typename... Rest>
        struct VariantMaxAlign<T, Rest...>
        {
            static constexpr std::size_t value = variant_max(alignof(T), VariantMaxAlign<Rest...>::value);
        };

        template <typename T, typename... Ts>
        struct VariantIndexOf;
        template <typename T, typename... Rest>
        struct VariantIndexOf<T, T, Rest...>
        {
            static constexpr std::size_t value = 0;
        };
        template <typename T, typename U, typename... Rest>
        struct VariantIndexOf<T, U, Rest...>
        {
            static constexpr std::size_t value = 1 + VariantIndexOf<T, Rest...>::value;
        };

        template <std::size_t I, typename... Ts>
        struct VariantTypeAt;
        template <typename T, typename... Rest>
        struct VariantTypeAt<0, T, Rest...>
        {
            using type = T;
        };
        template <std::size_t I, typename T, typename... Rest>
        struct VariantTypeAt<I, T, Rest...>
        {
            using type = typename VariantTypeAt<I - 1, Rest...>::type;
        };

        template <typename... Ts>
        struct VariantOps
        {
            static void destroy(std::size_t, void *) { fatal("ct::Variant: indice invalido"); }
            static void copy_construct(std::size_t, void *, const void *)
            {
                fatal("ct::Variant: indice invalido");
            }
            static void move_construct(std::size_t, void *, void *)
            {
                fatal("ct::Variant: indice invalido");
            }
        };
        template <typename T, typename... Rest>
        struct VariantOps<T, Rest...>
        {
            static void destroy(std::size_t idx, void *p)
            {
                if (idx == 0)
                    static_cast<T *>(p)->~T();
                else
                    VariantOps<Rest...>::destroy(idx - 1, p);
            }
            static void copy_construct(std::size_t idx, void *dst, const void *src)
            {
                if (idx == 0)
                    ::new (dst) T(*static_cast<const T *>(src));
                else
                    VariantOps<Rest...>::copy_construct(idx - 1, dst, src);
            }
            static void move_construct(std::size_t idx, void *dst, void *src)
            {
                if (idx == 0)
                    ::new (dst) T(move(*static_cast<T *>(src)));
                else
                    VariantOps<Rest...>::move_construct(idx - 1, dst, src);
            }
        };

        template <typename F, typename... Ts>
        struct VariantVisit
        {
            template <typename ReturnT, typename V>
            static ReturnT apply(std::size_t, V *, F &)
            {
                fatal("ct::Variant: indice invalido em visit");
            }
        };
        template <typename F, typename T, typename... Rest>
        struct VariantVisit<F, T, Rest...>
        {
            template <typename ReturnT, typename V>
            static ReturnT apply(std::size_t idx, V *p, F &f)
            {
                if (idx == 0)
                    return f(*static_cast<
                              typename detail::conditional<is_const<V>::value, const T, T>::type *>(p));
                return VariantVisit<F, Rest...>::template apply<ReturnT>(idx - 1, p, f);
            }
        };
    } 

    template <typename... Ts>
    class Variant
    {
    public:
        static_assert(sizeof...(Ts) > 0, "ct::Variant precisa de pelo menos um tipo");

        Variant() : index_(0) { ::new (static_cast<void *>(storage_)) First(); }

        template <typename T,
                  typename detail::enable_if<
                      !detail::is_same<typename std::decay<T>::type, Variant>::value, int>::type = 0>
        Variant(T value) : index_(detail::VariantIndexOf<typename std::decay<T>::type, Ts...>::value)
        {
            using D = typename std::decay<T>::type;
            ::new (static_cast<void *>(storage_)) D(detail::move(value));
        }

        Variant(const Variant &o) : index_(o.index_)
        {
            detail::VariantOps<Ts...>::copy_construct(index_, storage_, o.storage_);
        }
        Variant(Variant &&o) noexcept : index_(o.index_)
        {
            detail::VariantOps<Ts...>::move_construct(index_, storage_, o.storage_);
        }

        Variant &operator=(const Variant &o)
        {
            if (this == &o)
                return *this;
            detail::VariantOps<Ts...>::destroy(index_, storage_);
            detail::VariantOps<Ts...>::copy_construct(o.index_, storage_, o.storage_);
            index_ = o.index_;
            return *this;
        }
        Variant &operator=(Variant &&o) noexcept
        {
            if (this == &o)
                return *this;
            detail::VariantOps<Ts...>::destroy(index_, storage_);
            detail::VariantOps<Ts...>::move_construct(o.index_, storage_, o.storage_);
            index_ = o.index_;
            return *this;
        }

        template <typename T,
                  typename detail::enable_if<
                      !detail::is_same<typename std::decay<T>::type, Variant>::value, int>::type = 0>
        Variant &operator=(T value)
        {
            using D = typename std::decay<T>::type;
            constexpr std::size_t idx = detail::VariantIndexOf<D, Ts...>::value;
            detail::VariantOps<Ts...>::destroy(index_, storage_);
            ::new (static_cast<void *>(storage_)) D(detail::move(value));
            index_ = idx;
            return *this;
        }

        ~Variant() { detail::VariantOps<Ts...>::destroy(index_, storage_); }

        std::size_t index() const noexcept { return index_; }

        template <typename T>
        bool is() const noexcept
        {
            return index_ == detail::VariantIndexOf<T, Ts...>::value;
        }

        template <typename T>
        T &get()
        {
            if (!is<T>())
                detail::fatal("ct::Variant::get: tipo ativo diferente do pedido");
            return *reinterpret_cast<T *>(storage_);
        }
        template <typename T>
        const T &get() const
        {
            if (!is<T>())
                detail::fatal("ct::Variant::get: tipo ativo diferente do pedido");
            return *reinterpret_cast<const T *>(storage_);
        }

        template <typename T>
        T *get_if() noexcept
        {
            return is<T>() ? reinterpret_cast<T *>(storage_) : nullptr;
        }
        template <typename T>
        const T *get_if() const noexcept
        {
            return is<T>() ? reinterpret_cast<const T *>(storage_) : nullptr;
        }

        template <typename F>
        auto visit(F &&f)
            -> decltype(f(detail::declval<typename detail::VariantTypeAt<0, Ts...>::type &>()))
        {
            using ReturnT =
                decltype(f(detail::declval<typename detail::VariantTypeAt<0, Ts...>::type &>()));
            return detail::VariantVisit<F, Ts...>::template apply<ReturnT>(
                index_, static_cast<void *>(storage_), f);
        }
        template <typename F>
        auto visit(F &&f) const -> decltype(
            f(detail::declval<const typename detail::VariantTypeAt<0, Ts...>::type &>()))
        {
            using ReturnT = decltype(
                f(detail::declval<const typename detail::VariantTypeAt<0, Ts...>::type &>()));
            return detail::VariantVisit<F, Ts...>::template apply<ReturnT>(
                index_, static_cast<const void *>(storage_), f);
        }

        void swap(Variant &o) noexcept
        {
            if (this == &o)
                return;
            Variant tmp(detail::move(*this));
            *this = detail::move(o);
            o = detail::move(tmp);
        }

    private:
        using First = typename detail::VariantTypeAt<0, Ts...>::type;

        alignas(detail::VariantMaxAlign<Ts...>::value) unsigned char
            storage_[detail::VariantMaxSize<Ts...>::value];
        std::size_t index_;
    };

    template <typename... Ts>
    inline void swap(Variant<Ts...> &a, Variant<Ts...> &b) noexcept
    {
        a.swap(b);
    }

} 