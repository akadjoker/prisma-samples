#include <ct/thread.hpp>
#include <ct/threadpool.hpp>

#include <gtest/gtest.h>

#include <utility>
#include <vector>

using ct::Atomic;
using ct::CondVar;
using ct::LockGuard;
using ct::Mutex;
using ct::Thread;
using ct::ThreadPool;

TEST(Thread, RunsAndJoins)
{
    int value = 0;
    Thread t([&] { value = 42; });
    EXPECT_TRUE(t.joinable());
    t.join();
    EXPECT_FALSE(t.joinable());
    EXPECT_EQ(value, 42);
}

TEST(Thread, DestructorJoins)
{
    Atomic<int> done(0);
    {
        Thread t([&] {
            Thread::sleep_ms(5);
            done.store(1);
        });
    }
    EXPECT_EQ(done.load(), 1);
}

TEST(Thread, MoveTransfersOwnership)
{
    Atomic<int> v(0);
    Thread a([&] { v.fetch_add(1); });
    Thread b(ct::detail::move(a));
    EXPECT_FALSE(a.joinable());
    EXPECT_TRUE(b.joinable());
    b.join();
    EXPECT_EQ(v.load(), 1);
    Thread c;
    c = Thread([&] { v.fetch_add(1); });
    c.join();
    EXPECT_EQ(v.load(), 2);
}

TEST(Thread, StartLater)
{
    Thread t;
    EXPECT_FALSE(t.joinable());
    int x = 0;
    t.start([&] { x = 7; });
    t.join();
    EXPECT_EQ(x, 7);
    EXPECT_GE(Thread::hardware_concurrency(), 1u);
    Thread::yield();
}

TEST(Atomic, CountsAcrossThreads)
{
    Atomic<int> counter(0);
    Atomic<long long> big(0);
    ct::Vector<Thread> threads;
    for (int i = 0; i < 8; ++i)
        threads.emplace_back(Thread::Fn([&] {
            for (int k = 0; k < 10000; ++k)
            {
                ++counter;
                big += 3;
            }
        }));
    for (std::size_t i = 0; i < threads.size(); ++i)
        threads[i].join();
    EXPECT_EQ(counter.load(), 80000);
    EXPECT_EQ(big.load(), 240000LL);
}

TEST(Atomic, ExchangeAndCompareExchange)
{
    Atomic<unsigned> a(5);
    EXPECT_EQ(a.exchange(9), 5u);
    unsigned expected = 9;
    EXPECT_TRUE(a.compare_exchange(expected, 11));
    EXPECT_EQ(a.load(), 11u);
    expected = 3;
    EXPECT_FALSE(a.compare_exchange(expected, 1));
    EXPECT_EQ(expected, 11u);
    EXPECT_EQ(a--, 11u);
    EXPECT_EQ(--a, 9u);
    a = 2;
    EXPECT_EQ(unsigned(a), 2u);
    int dummy = 0;
    Atomic<int *> p(nullptr);
    EXPECT_EQ(p.load(), nullptr);
    p.store(&dummy);
    EXPECT_EQ(p.load(), &dummy);
}

TEST(Mutex, ProtectsCounter)
{
    Mutex m;
    long counter = 0;
    ct::Vector<Thread> threads;
    for (int i = 0; i < 4; ++i)
        threads.emplace_back(Thread::Fn([&] {
            for (int k = 0; k < 20000; ++k)
            {
                LockGuard g(m);
                ++counter;
            }
        }));
    for (std::size_t i = 0; i < threads.size(); ++i)
        threads[i].join();
    EXPECT_EQ(counter, 80000);
    EXPECT_TRUE(m.try_lock());
    EXPECT_FALSE(m.try_lock());
    m.unlock();
}

TEST(CondVar, ProducerConsumer)
{
    Mutex m;
    CondVar cv;
    ct::Vector<int> queue;
    bool done = false;
    long sum = 0;
    Thread consumer([&] {
        for (;;)
        {
            LockGuard g(m);
            cv.wait(m, [&] { return !queue.empty() || done; });
            while (!queue.empty())
            {
                sum += queue.back();
                queue.pop_back();
            }
            if (done)
                return;
        }
    });
    for (int i = 1; i <= 1000; ++i)
    {
        {
            LockGuard g(m);
            queue.push_back(i);
        }
        cv.notify_one();
    }
    {
        LockGuard g(m);
        done = true;
    }
    cv.notify_all();
    consumer.join();
    EXPECT_EQ(sum, 500500);
}

TEST(ThreadPool, SubmitAndWaitAll)
{
    ThreadPool pool(4);
    EXPECT_EQ(pool.size(), 4u);
    Atomic<int> counter(0);
    for (int i = 0; i < 1000; ++i)
        pool.submit([&] { counter.fetch_add(1); });
    pool.wait_all();
    EXPECT_EQ(counter.load(), 1000);
    EXPECT_EQ(pool.pending(), 0u);
    pool.wait_all();
}

TEST(ThreadPool, ParallelForCoversEveryIndexOnce)
{
    ThreadPool pool(3);
    std::vector<int> hits(100000, 0);
    pool.parallel_for(0, hits.size(), [&](std::size_t i) { hits[i] += 1; });
    for (std::size_t i = 0; i < hits.size(); ++i)
        ASSERT_EQ(hits[i], 1) << i;
    std::vector<int> small(7, 0);
    pool.parallel_for(2, 7, [&](std::size_t i) { small[i] = int(i); }, 1);
    EXPECT_EQ(small[1], 0);
    for (std::size_t i = 2; i < 7; ++i)
        EXPECT_EQ(small[i], int(i));
    pool.parallel_for(5, 5, [&](std::size_t) { FAIL(); });
}

TEST(ThreadPool, ParallelForSumsWithAtomic)
{
    ThreadPool pool;
    Atomic<long long> sum(0);
    pool.parallel_for(1, 100001, [&](std::size_t i) { sum.fetch_add(static_cast<long long>(i)); });
    EXPECT_EQ(sum.load(), 5000050000LL);
}

TEST(ThreadPool, NestedParallelForFromJobsDoesNotDeadlock)
{
    ThreadPool pool(2);
    Atomic<int> total(0);
    for (int j = 0; j < 8; ++j)
        pool.submit([&] { pool.parallel_for(0, 1000, [&](std::size_t) { total.fetch_add(1); }); });
    pool.wait_all();
    EXPECT_EQ(total.load(), 8000);
}

TEST(ThreadPool, DestructorRunsPendingJobs)
{
    Atomic<int> counter(0);
    {
        ThreadPool pool(2);
        for (int i = 0; i < 200; ++i)
            pool.submit([&] {
                Thread::yield();
                counter.fetch_add(1);
            });
    }
    EXPECT_EQ(counter.load(), 200);
}

TEST(ThreadPool, SingleWorkerStillParallelForWithCallerHelping)
{
    ThreadPool pool(1);
    Atomic<int> n(0);
    pool.parallel_for(0, 5000, [&](std::size_t) { n.fetch_add(1); });
    EXPECT_EQ(n.load(), 5000);
}

namespace
{
    template <typename F>
    bool runs_within_ms(unsigned budget_ms, F &&body)
    {
        Atomic<int> finished(0);
        Thread runner([&] {
            body();
            finished.store(1);
        });
        for (unsigned waited = 0; waited < budget_ms && finished.load() == 0; waited += 5)
            Thread::sleep_ms(5);
        if (finished.load() == 0)
        {
            runner.detach();
            return false;
        }
        runner.join();
        return true;
    }
}

TEST(ThreadPool, WaitAllFromInsideAJobDoesNotDeadlock)
{
    ThreadPool *pool = new ThreadPool(1);
    Atomic<int> done(0);
    const bool ok = runs_within_ms(10000, [&] {
        pool->submit([&] {
            for (int i = 0; i < 10; ++i)
                pool->submit([&] { done.fetch_add(1); });
            pool->wait_all();
            done.fetch_add(1);
        });
        pool->wait_all();
    });
    ASSERT_TRUE(ok) << "wait_all chamado de dentro de um job bloqueou";
    EXPECT_EQ(done.load(), 11);
    delete pool;
}

TEST(ThreadPool, NestedWaitAllWaitsForItsOwnChildren)
{
    ThreadPool *pool = new ThreadPool(2);
    Atomic<int> children(0);
    Atomic<int> seen_by_parent(0);
    const bool ok = runs_within_ms(10000, [&] {
        pool->submit([&] {
            for (int i = 0; i < 50; ++i)
                pool->submit([&] {
                    Thread::yield();
                    children.fetch_add(1);
                });
            pool->wait_all();
            seen_by_parent.store(children.load());
        });
        pool->wait_all();
    });
    ASSERT_TRUE(ok);
    EXPECT_EQ(seen_by_parent.load(), 50);
    delete pool;
}

TEST(ThreadPool, TwoThreadsWaitingAllAreBothWoken)
{
    ThreadPool *pool = new ThreadPool(1);
    Mutex gate;
    CondVar gate_cv;
    bool open = false;
    Atomic<int> released(0);
    const bool ok = runs_within_ms(10000, [&] {
        pool->submit([&] {
            LockGuard g(gate);
            while (!open)
                gate_cv.wait(gate);
        });
        pool->submit([&] {
            LockGuard g(gate);
            while (!open)
                gate_cv.wait(gate);
        });
        Thread a([&] {
            pool->wait_all();
            released.fetch_add(1);
        });
        Thread b([&] {
            pool->wait_all();
            released.fetch_add(1);
        });
        Thread::sleep_ms(50);
        {
            LockGuard g(gate);
            open = true;
        }
        gate_cv.notify_all();
        a.join();
        b.join();
    });
    ASSERT_TRUE(ok) << "um dos wait_all nunca acordou";
    EXPECT_EQ(released.load(), 2);
    EXPECT_EQ(pool->pending(), 0u);
    delete pool;
}

TEST(ThreadPool, DoisJobsComWaitAllAoMesmoTempoTerminamAmbos)
{
    ThreadPool *pool = new ThreadPool(2);
    Atomic<int> started(0);
    Atomic<int> done(0);
    const bool ok = runs_within_ms(10000, [&] {
        for (int j = 0; j < 2; ++j)
        {
            pool->submit([&] {
                started.fetch_add(1);
                while (started.load() < 2)
                    Thread::yield();
                pool->submit([&] { done.fetch_add(1); });
                pool->wait_all();
                done.fetch_add(1);
            });
        }
        pool->wait_all();
    });
    ASSERT_TRUE(ok) << "dois wait_all aninhados em simultaneo ficaram bloqueados";
    EXPECT_EQ(done.load(), 4);
    delete pool;
}

TEST(ThreadPool, JobSubmetidoDuranteParallelForAcordaWaitAllAninhadoAdormecido)
{
    ThreadPool *pool = new ThreadPool(1);
    Atomic<int> running(0);
    Atomic<int> waiter_done(0);
    Atomic<int> job_done(0);
    const bool ok = runs_within_ms(10000, [&] {
        pool->submit([&] {
            while (running.load() == 0)
                Thread::yield();
            pool->wait_all();
            waiter_done.store(1);
        });
        Thread::sleep_ms(50);
        pool->parallel_for(0, 1, [&](std::size_t) {
            running.store(1);
            Thread::sleep_ms(200);
            pool->submit([&] { job_done.store(1); });
        });
        for (int waited = 0; waited < 3000 && (waiter_done.load() == 0 || job_done.load() == 0); waited += 5)
            Thread::sleep_ms(5);
    });
    ASSERT_TRUE(ok);
    EXPECT_EQ(waiter_done.load(), 1) << "o waiter aninhado ficou adormecido com trabalho pendente";
    EXPECT_EQ(job_done.load(), 1);
    pool->wait_all();
    delete pool;
}

TEST(Atomic, PonteirosAvancamEmElementosENaoEmBytes)
{
    int values[16] = {};
    Atomic<int *> p(values);
    EXPECT_EQ(++p, values + 1);
    EXPECT_EQ(p++, values + 1);
    EXPECT_EQ(p.load(), values + 2);
    EXPECT_EQ(p += 3, values + 5);
    EXPECT_EQ(p -= 2, values + 3);
    EXPECT_EQ(--p, values + 2);
    EXPECT_EQ(p--, values + 2);
    EXPECT_EQ(p.load(), values + 1);
    struct Wide { char bytes[24]; } wide[8];
    Atomic<Wide *> q(wide);
    q += 5;
    EXPECT_EQ(q.load(), wide + 5);
    EXPECT_EQ(reinterpret_cast<char *>(q.load()) - reinterpret_cast<char *>(wide), 5 * 24);
}

TEST(Atomic, PonteirosSaoAtomicosEntreThreads)
{
    static int slots[40000];
    Atomic<int *> cursor(slots);
    ct::Vector<Thread> threads;
    for (int i = 0; i < 4; ++i)
        threads.emplace_back(Thread::Fn([&] {
            for (int k = 0; k < 10000; ++k)
                ++cursor;
        }));
    for (std::size_t i = 0; i < threads.size(); ++i)
        threads[i].join();
    EXPECT_EQ(cursor.load(), slots + 40000);
}

TEST(Thread, MoverUmaThreadNaoIniciadaNaoLeLixo)
{
    Thread idle;
    Thread moved(std::move(idle));
    EXPECT_FALSE(moved.joinable());
    EXPECT_FALSE(idle.joinable());
    Thread assigned;
    assigned = std::move(moved);
    EXPECT_FALSE(assigned.joinable());
    Atomic<int> ran(0);
    Thread live(Thread::Fn([&] { ran.store(1); }));
    assigned = std::move(live);
    EXPECT_TRUE(assigned.joinable());
    assigned.join();
    EXPECT_EQ(ran.load(), 1);
}

TEST(ThreadPool, TempestadeDeWaitAllAninhadosTerminaSempre)
{
    for (int round = 0; round < 20; ++round)
    {
        ThreadPool *pool = new ThreadPool(1 + static_cast<unsigned>(round % 3));
        Atomic<int> done(0);
        const bool ok = runs_within_ms(20000, [&] {
            for (int a = 0; a < 6; ++a)
            {
                pool->submit([&, a] {
                    for (int b = 0; b < 4; ++b)
                        pool->submit([&, b] {
                            pool->submit([&] { done.fetch_add(1); });
                            if (b % 2)
                                pool->wait_all();
                            done.fetch_add(1);
                        });
                    if (a % 2)
                        pool->wait_all();
                    pool->parallel_for(0, 8, [&](std::size_t) { done.fetch_add(1); });
                });
            }
            pool->wait_all();
        });
        ASSERT_TRUE(ok) << "ronda " << round << " ficou bloqueada";
        EXPECT_EQ(done.load(), 6 * 4 * 2 + 6 * 8) << "ronda " << round;
        delete pool;
    }
}
