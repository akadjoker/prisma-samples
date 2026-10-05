#pragma once

#include "detail/utils.hpp"
#include "function.hpp"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <process.h>
#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#endif
#else
#include <pthread.h>
#include <sched.h>
#include <time.h>
#include <unistd.h>
#endif

namespace ct
{
    namespace detail
    {
#if defined(_MSC_VER) && !defined(__clang__)
        template <std::size_t N>
        struct AtomicOps;

        template <>
        struct AtomicOps<4>
        {
            using Raw = long;
            static Raw load(volatile Raw *p) noexcept { return _InterlockedOr(p, 0); }
            static void store(volatile Raw *p, Raw v) noexcept { _InterlockedExchange(p, v); }
            static Raw exchange(volatile Raw *p, Raw v) noexcept { return _InterlockedExchange(p, v); }
            static Raw fetch_add(volatile Raw *p, Raw v) noexcept { return _InterlockedExchangeAdd(p, v); }
            static Raw cas(volatile Raw *p, Raw expected, Raw desired) noexcept
            {
                return _InterlockedCompareExchange(p, desired, expected);
            }
        };

        template <>
        struct AtomicOps<8>
        {
            using Raw = __int64;
            static Raw load(volatile Raw *p) noexcept { return _InterlockedOr64(p, 0); }
            static void store(volatile Raw *p, Raw v) noexcept { _InterlockedExchange64(p, v); }
            static Raw exchange(volatile Raw *p, Raw v) noexcept { return _InterlockedExchange64(p, v); }
            static Raw fetch_add(volatile Raw *p, Raw v) noexcept { return _InterlockedExchangeAdd64(p, v); }
            static Raw cas(volatile Raw *p, Raw expected, Raw desired) noexcept
            {
                return _InterlockedCompareExchange64(p, desired, expected);
            }
        };
#endif
    }

    template <typename T>
    class Atomic
    {
        static_assert(std::is_integral<T>::value || std::is_pointer<T>::value,
                      "ct::Atomic: so inteiros e ponteiros");
        static_assert(sizeof(T) == 4 || sizeof(T) == 8, "ct::Atomic: so 32 ou 64 bits");

        using IsPointer = detail::integral_constant<bool, std::is_pointer<T>::value>;
        using Diff = typename std::conditional<std::is_pointer<T>::value, std::ptrdiff_t, T>::type;

    public:
        Atomic() noexcept : v_(T()) {}
        explicit Atomic(T v) noexcept : v_(v) {}
        Atomic(const Atomic &) = delete;
        Atomic &operator=(const Atomic &) = delete;

#if defined(_MSC_VER) && !defined(__clang__)
        using Ops = detail::AtomicOps<sizeof(T)>;
        using Raw = typename Ops::Raw;

        T load() const noexcept { return from_raw(Ops::load(raw())); }
        void store(T v) noexcept { Ops::store(raw(), to_raw(v)); }
        T exchange(T v) noexcept { return from_raw(Ops::exchange(raw(), to_raw(v))); }
        T fetch_add(T v) noexcept { return from_raw(Ops::fetch_add(raw(), to_raw(v))); }
        T fetch_sub(T v) noexcept { return from_raw(Ops::fetch_add(raw(), static_cast<Raw>(0) - to_raw(v))); }
        bool compare_exchange(T &expected, T desired) noexcept
        {
            Raw old = Ops::cas(raw(), to_raw(expected), to_raw(desired));
            if (old == to_raw(expected))
                return true;
            expected = from_raw(old);
            return false;
        }

    private:
        volatile Raw *raw() const noexcept { return reinterpret_cast<volatile Raw *>(const_cast<T *>(&v_)); }
        static Raw to_raw(T v) noexcept
        {
            Raw r;
            std::memcpy(&r, &v, sizeof(T));
            return r;
        }
        static T from_raw(Raw r) noexcept
        {
            T v;
            std::memcpy(&v, &r, sizeof(T));
            return v;
        }
#else
        T load() const noexcept { return __atomic_load_n(&v_, __ATOMIC_SEQ_CST); }
        void store(T v) noexcept { __atomic_store_n(&v_, v, __ATOMIC_SEQ_CST); }
        T exchange(T v) noexcept { return __atomic_exchange_n(&v_, v, __ATOMIC_SEQ_CST); }
        T fetch_add(T v) noexcept { return __atomic_fetch_add(&v_, v, __ATOMIC_SEQ_CST); }
        T fetch_sub(T v) noexcept { return __atomic_fetch_sub(&v_, v, __ATOMIC_SEQ_CST); }
        bool compare_exchange(T &expected, T desired) noexcept
        {
            return __atomic_compare_exchange_n(&v_, &expected, desired, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
        }

    private:
#endif
        T v_;

    public:
        operator T() const noexcept { return load(); }
        T operator=(T v) noexcept
        {
            store(v);
            return v;
        }
        T operator++() noexcept { return step(1, false, IsPointer()) + 1; }
        T operator++(int) noexcept { return step(1, false, IsPointer()); }
        T operator--() noexcept { return step(1, true, IsPointer()) - 1; }
        T operator--(int) noexcept { return step(1, true, IsPointer()); }
        T operator+=(Diff v) noexcept { return step(v, false, IsPointer()) + v; }
        T operator-=(Diff v) noexcept { return step(v, true, IsPointer()) - v; }

    private:
        T step(Diff d, bool subtract, detail::false_type) noexcept
        {
            return subtract ? fetch_sub(d) : fetch_add(d);
        }
        T step(Diff d, bool subtract, detail::true_type) noexcept
        {
            T current = load();
            while (!compare_exchange(current, subtract ? current - d : current + d))
            {
            }
            return current;
        }
    };

    class Mutex
    {
    public:
        Mutex() noexcept
        {
#if defined(_WIN32)
            InitializeSRWLock(&lock_);
#else
            if (pthread_mutex_init(&lock_, nullptr) != 0)
                detail::fatal("ct::Mutex: pthread_mutex_init falhou");
#endif
        }
        ~Mutex()
        {
#if !defined(_WIN32)
            pthread_mutex_destroy(&lock_);
#endif
        }
        Mutex(const Mutex &) = delete;
        Mutex &operator=(const Mutex &) = delete;

        void lock() noexcept
        {
#if defined(_WIN32)
            AcquireSRWLockExclusive(&lock_);
#else
            pthread_mutex_lock(&lock_);
#endif
        }
        bool try_lock() noexcept
        {
#if defined(_WIN32)
            return TryAcquireSRWLockExclusive(&lock_) != 0;
#else
            return pthread_mutex_trylock(&lock_) == 0;
#endif
        }
        void unlock() noexcept
        {
#if defined(_WIN32)
            ReleaseSRWLockExclusive(&lock_);
#else
            pthread_mutex_unlock(&lock_);
#endif
        }

    private:
        friend class CondVar;
#if defined(_WIN32)
        SRWLOCK lock_;
#else
        pthread_mutex_t lock_;
#endif
    };

    class LockGuard
    {
    public:
        explicit LockGuard(Mutex &m) noexcept : m_(m) { m_.lock(); }
        ~LockGuard() { m_.unlock(); }
        LockGuard(const LockGuard &) = delete;
        LockGuard &operator=(const LockGuard &) = delete;

    private:
        Mutex &m_;
    };

    class CondVar
    {
    public:
        CondVar() noexcept
        {
#if defined(_WIN32)
            InitializeConditionVariable(&cv_);
#else
            if (pthread_cond_init(&cv_, nullptr) != 0)
                detail::fatal("ct::CondVar: pthread_cond_init falhou");
#endif
        }
        ~CondVar()
        {
#if !defined(_WIN32)
            pthread_cond_destroy(&cv_);
#endif
        }
        CondVar(const CondVar &) = delete;
        CondVar &operator=(const CondVar &) = delete;

        void wait(Mutex &m) noexcept
        {
#if defined(_WIN32)
            SleepConditionVariableSRW(&cv_, &m.lock_, INFINITE, 0);
#else
            pthread_cond_wait(&cv_, &m.lock_);
#endif
        }
        template <typename Pred>
        void wait(Mutex &m, Pred pred) noexcept
        {
            while (!pred())
                wait(m);
        }
        void notify_one() noexcept
        {
#if defined(_WIN32)
            WakeConditionVariable(&cv_);
#else
            pthread_cond_signal(&cv_);
#endif
        }
        void notify_all() noexcept
        {
#if defined(_WIN32)
            WakeAllConditionVariable(&cv_);
#else
            pthread_cond_broadcast(&cv_);
#endif
        }

    private:
#if defined(_WIN32)
        CONDITION_VARIABLE cv_;
#else
        pthread_cond_t cv_;
#endif
    };

    class Thread : private HeapAlloc
    {
    public:
        using Fn = Function<void()>;

        Thread() noexcept : handle_(), joinable_(false) {}
        explicit Thread(Fn fn) : handle_(), joinable_(false) { start(detail::move(fn)); }
        Thread(Thread &&o) noexcept : handle_(o.handle_), joinable_(o.joinable_) { o.joinable_ = false; }
        Thread &operator=(Thread &&o) noexcept
        {
            if (this != &o)
            {
                if (joinable_)
                    join();
                handle_ = o.handle_;
                joinable_ = o.joinable_;
                o.joinable_ = false;
            }
            return *this;
        }
        Thread(const Thread &) = delete;
        Thread &operator=(const Thread &) = delete;
        ~Thread()
        {
            if (joinable_)
                join();
        }

        void start(Fn fn)
        {
            if (joinable_)
                detail::fatal("ct::Thread::start: thread ja esta a correr");
            if (!fn)
                detail::fatal("ct::Thread::start: funcao vazia");
            Block *b = static_cast<Block *>(this->allocate(sizeof(Block), alignof(Block)));
            new (b) Block(detail::move(fn));
#if defined(_WIN32)
            uintptr_t h = _beginthreadex(nullptr, 0, &Thread::run, b, 0, nullptr);
            if (h == 0)
            {
                b->~Block();
                this->deallocate(b, sizeof(Block));
                detail::fatal("ct::Thread::start: _beginthreadex falhou");
            }
            handle_ = reinterpret_cast<HANDLE>(h);
#else
            if (pthread_create(&handle_, nullptr, &Thread::run, b) != 0)
            {
                b->~Block();
                this->deallocate(b, sizeof(Block));
                detail::fatal("ct::Thread::start: pthread_create falhou");
            }
#endif
            joinable_ = true;
        }

        bool joinable() const noexcept { return joinable_; }

        void join()
        {
            if (!joinable_)
                detail::fatal("ct::Thread::join: thread nao e joinable");
#if defined(_WIN32)
            WaitForSingleObject(handle_, INFINITE);
            CloseHandle(handle_);
#else
            pthread_join(handle_, nullptr);
#endif
            joinable_ = false;
        }

        void detach()
        {
            if (!joinable_)
                detail::fatal("ct::Thread::detach: thread nao e joinable");
#if defined(_WIN32)
            CloseHandle(handle_);
#else
            pthread_detach(handle_);
#endif
            joinable_ = false;
        }

        static unsigned hardware_concurrency() noexcept
        {
#if defined(_WIN32)
            SYSTEM_INFO info;
            GetSystemInfo(&info);
            return info.dwNumberOfProcessors > 0 ? static_cast<unsigned>(info.dwNumberOfProcessors) : 1u;
#else
            long n = sysconf(_SC_NPROCESSORS_ONLN);
            return n > 0 ? static_cast<unsigned>(n) : 1u;
#endif
        }

        static void yield() noexcept
        {
#if defined(_WIN32)
            SwitchToThread();
#else
            sched_yield();
#endif
        }

        static void sleep_ms(unsigned ms) noexcept
        {
#if defined(_WIN32)
            Sleep(ms);
#else
            timespec ts;
            ts.tv_sec = static_cast<time_t>(ms / 1000);
            ts.tv_nsec = static_cast<long>(ms % 1000) * 1000000L;
            nanosleep(&ts, nullptr);
#endif
        }

        static std::uint64_t monotonic_ms() noexcept
        {
#if defined(_WIN32)
            return static_cast<std::uint64_t>(GetTickCount64());
#else
            timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            return static_cast<std::uint64_t>(ts.tv_sec) * 1000u + static_cast<std::uint64_t>(ts.tv_nsec / 1000000L);
#endif
        }

    private:
        struct Block
        {
            Fn fn;
            explicit Block(Fn f) : fn(detail::move(f)) {}
        };

#if defined(_WIN32)
        static unsigned __stdcall run(void *arg)
#else
        static void *run(void *arg)
#endif
        {
            Block *b = static_cast<Block *>(arg);
            Fn fn(detail::move(b->fn));
            b->~Block();
            HeapAlloc alloc;
            alloc.deallocate(b, sizeof(Block));
            fn();
#if defined(_WIN32)
            return 0;
#else
            return nullptr;
#endif
        }

#if defined(_WIN32)
        HANDLE handle_;
#else
        pthread_t handle_;
#endif
        bool joinable_;
    };
}
