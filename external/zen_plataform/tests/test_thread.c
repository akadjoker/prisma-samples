/*
 * test_thread.c - threads, recursive mutexes, condition variables, detach, timeouts.
 */
#include "platform.h"

#include <stdio.h>
#include <string.h>

static int g_pass, g_fail;

#define CHECK(cond)                                                \
    do                                                             \
    {                                                              \
        if (cond)                                                  \
            g_pass++;                                              \
        else                                                       \
        {                                                          \
            g_fail++;                                              \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
        }                                                          \
    } while (0)

/* ---- join result, ids, many threads on one counter ---- */

static int returns_value(void *user)
{
    return *(int *)user * 2;
}

typedef struct
{
    PlatformMutex *m;
    long *counter;
    int times;
} Counter;

static int bump(void *user)
{
    Counter *c = user;
    for (int i = 0; i < c->times; i++)
    {
        mutex_lock(c->m);
        mutex_lock(c->m); /* recursive */
        ++*c->counter;
        mutex_unlock(c->m);
        mutex_unlock(c->m);
    }
    return 0;
}

static uint64_t g_other_id;
static int record_id(void *user)
{
    (void)user;
    g_other_id = thread_current_id();
    return 0;
}

static void test_basic(void)
{
    int arg = 21;
    PlatformThread *t = thread_create(returns_value, &arg, "worker");
    CHECK(t != NULL);
    CHECK(t && thread_join(t) == 42);

    CHECK(thread_create(NULL, NULL, NULL) == NULL);
    CHECK(platform_get_error()[0] != '\0');

    t = thread_create(record_id, NULL, NULL);
    CHECK(t != NULL);
    if (t)
        thread_join(t);
    CHECK(g_other_id != 0 && g_other_id != thread_current_id());

    CHECK(cpu_count() >= 1);
}

static void test_mutex_counter(void)
{
    PlatformMutex *m = mutex_create();
    CHECK(m != NULL);
    long counter = 0;
    Counter c = {m, &counter, 20000};
    PlatformThread *ts[8];
    for (int i = 0; i < 8; i++)
        ts[i] = thread_create(bump, &c, "bump");
    for (int i = 0; i < 8; i++)
        CHECK(ts[i] && thread_join(ts[i]) == 0);
    CHECK(counter == 8 * 20000L);
    mutex_destroy(m);
}

/* ---- try_lock across threads ---- */

typedef struct
{
    PlatformMutex *m;
    bool got;
} TryArg;

static int try_it(void *user)
{
    TryArg *a = user;
    a->got = mutex_try_lock(a->m);
    if (a->got)
        mutex_unlock(a->m);
    return 0;
}

static void test_try_lock(void)
{
    PlatformMutex *m = mutex_create();
    TryArg a = {m, true};

    mutex_lock(m);
    CHECK(mutex_try_lock(m)); /* the holder may lock again */
    mutex_unlock(m);
    PlatformThread *t = thread_create(try_it, &a, NULL);
    thread_join(t);
    CHECK(!a.got); /* another thread may not */

    mutex_unlock(m);
    t = thread_create(try_it, &a, NULL);
    thread_join(t);
    CHECK(a.got);
    mutex_destroy(m);
}

/* ---- producer / consumer on a condition ---- */

#define QUEUE_N 8
typedef struct
{
    PlatformMutex *m;
    PlatformCond *not_empty, *not_full;
    int items[QUEUE_N];
    int head, count;
    long sum;
} Queue;

static int consumer(void *user)
{
    Queue *q = user;
    for (;;)
    {
        mutex_lock(q->m);
        while (q->count == 0)
            cond_wait(q->not_empty, q->m);
        int v = q->items[q->head];
        q->head = (q->head + 1) % QUEUE_N;
        q->count--;
        cond_signal(q->not_full);
        mutex_unlock(q->m);
        if (v < 0)
            return 0;
        q->sum += v;
    }
}

static void test_cond(void)
{
    Queue q = {0};
    q.m = mutex_create();
    q.not_empty = cond_create();
    q.not_full = cond_create();
    CHECK(q.m && q.not_empty && q.not_full);

    PlatformThread *t = thread_create(consumer, &q, "consumer");
    long expect = 0;
    for (int i = 0; i <= 500; i++)
    {
        int v = i == 500 ? -1 : i; /* -1 ends the consumer */
        if (i < 500)
            expect += i;
        mutex_lock(q.m);
        while (q.count == QUEUE_N)
            cond_wait(q.not_full, q.m);
        q.items[(q.head + q.count) % QUEUE_N] = v;
        q.count++;
        cond_signal(q.not_empty);
        mutex_unlock(q.m);
    }
    CHECK(t && thread_join(t) == 0);
    CHECK(q.sum == expect);

    mutex_destroy(q.m);
    cond_destroy(q.not_empty);
    cond_destroy(q.not_full);
}

/* ---- timeouts ---- */

typedef struct
{
    PlatformMutex *m;
    PlatformCond *c;
} Signal;

static int signal_later(void *user)
{
    Signal *s = user;
    time_sleep(30);
    mutex_lock(s->m);
    cond_signal(s->c);
    mutex_unlock(s->m);
    return 0;
}

static void test_timeout(void)
{
    Signal s = {mutex_create(), cond_create()};

    mutex_lock(s.m);
    double t0 = time_seconds();
    bool signalled = cond_wait_timeout(s.c, s.m, 60);
    double dt = time_seconds() - t0;
    mutex_unlock(s.m);
    CHECK(!signalled);
    CHECK(dt >= 0.05 && dt < 1.0);

    PlatformThread *t = thread_create(signal_later, &s, NULL);
    mutex_lock(s.m);
    t0 = time_seconds();
    signalled = cond_wait_timeout(s.c, s.m, 5000);
    dt = time_seconds() - t0;
    mutex_unlock(s.m);
    CHECK(signalled);
    CHECK(dt < 2.0);
    thread_join(t);

    mutex_destroy(s.m);
    cond_destroy(s.c);
}

/* ---- detach ---- */

static volatile int g_detached_done;
static int detached_work(void *user)
{
    (void)user;
    time_sleep(20);
    g_detached_done = 1;
    return 0;
}

static void test_detach(void)
{
    PlatformThread *t = thread_create(detached_work, NULL, "detached");
    thread_detach(t); /* before it finishes: the thread frees its own record */
    for (int i = 0; i < 200 && !g_detached_done; i++)
        time_sleep(5);
    CHECK(g_detached_done);

    g_detached_done = 0;
    t = thread_create(detached_work, NULL, NULL);
    for (int i = 0; i < 200 && !g_detached_done; i++)
        time_sleep(5);
    time_sleep(20);
    thread_detach(t); /* after it finished: detach frees the record */
    CHECK(g_detached_done);
    time_sleep(20);
}

int main(void)
{
    test_basic();
    test_mutex_counter();
    test_try_lock();
    test_cond();
    test_timeout();
    test_detach();
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
