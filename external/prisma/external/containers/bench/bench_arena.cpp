
#include <ct/arena.hpp>
#include <ct/vector.hpp>

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "bench.hpp"

volatile std::uint64_t bench::sink = 0;

namespace
{

    constexpr int NALLOC = 1000000; 
    constexpr int NVEC = 2000000;   

    struct Node
    {
        int v;
        Node *next;
    };

    std::uint64_t arena_alloc_64()
    {
        ct::Arena a(1 << 20);
        std::uint64_t acc = 0;
        for (int i = 0; i < NALLOC; ++i)
        {
            void *p = a.allocate(64, 16);
            acc += reinterpret_cast<std::uintptr_t>(p) & 0xFF;
        }
        return acc;
    }

    std::uint64_t malloc_alloc_64()
    {
        std::uint64_t acc = 0;
        std::vector<void *> ptrs;
        ptrs.reserve(NALLOC);
        for (int i = 0; i < NALLOC; ++i)
        {
            void *p = std::malloc(64);
            acc += reinterpret_cast<std::uintptr_t>(p) & 0xFF;
            ptrs.push_back(p);
        }
        for (void *p : ptrs)
            std::free(p);
        return acc;
    }

    std::uint64_t arena_list()
    {
        ct::Arena a(1 << 20);
        Node *head = nullptr;
        for (int i = 0; i < NALLOC / 2; ++i)
        {
            Node *n = a.create<Node>();
            n->v = i;
            n->next = head;
            head = n;
        }
        std::uint64_t s = 0;
        for (Node *n = head; n; n = n->next)
            s += static_cast<std::uint64_t>(n->v);
        return s;
    }

    std::uint64_t new_list()
    {
        Node *head = nullptr;
        for (int i = 0; i < NALLOC / 2; ++i)
        {
            Node *n = new Node;
            n->v = i;
            n->next = head;
            head = n;
        }
        std::uint64_t s = 0;
        for (Node *n = head; n;)
        {
            s += static_cast<std::uint64_t>(n->v);
            Node *nx = n->next;
            delete n;
            n = nx;
        }
        return s;
    }

    std::uint64_t arena_frames()
    {
        ct::Arena a(1 << 20);
        std::uint64_t acc = 0;
        for (int frame = 0; frame < 1000; ++frame)
        {
            for (int i = 0; i < 1000; ++i)
            {
                int *p = a.allocate_array<int>(16);
                p[0] = i;
                acc += static_cast<std::uint64_t>(p[0]);
            }
            a.reset();
        }
        return acc;
    }

    std::uint64_t malloc_frames()
    {
        std::uint64_t acc = 0;
        void *ptrs[1000];
        for (int frame = 0; frame < 1000; ++frame)
        {
            for (int i = 0; i < 1000; ++i)
            {
                int *p = static_cast<int *>(std::malloc(16 * sizeof(int)));
                p[0] = i;
                acc += static_cast<std::uint64_t>(p[0]);
                ptrs[i] = p;
            }
            for (int i = 0; i < 1000; ++i)
                std::free(ptrs[i]);
        }
        return acc;
    }

    std::uint64_t arena_vec_push()
    {
        ct::Arena a(1 << 20);
        ct::Vector<int, ct::ArenaAlloc> v{ct::ArenaAlloc(a)};
        for (int i = 0; i < NVEC; ++i)
            v.push_back(i);
        return static_cast<std::uint64_t>(v.back()) + v.size();
    }

    std::uint64_t heap_vec_push()
    {
        ct::Vector<int> v;
        for (int i = 0; i < NVEC; ++i)
            v.push_back(i);
        return static_cast<std::uint64_t>(v.back()) + v.size();
    }

    std::uint64_t std_vec_push()
    {
        std::vector<int> v;
        for (int i = 0; i < NVEC; ++i)
            v.push_back(i);
        return static_cast<std::uint64_t>(v.back()) + v.size();
    }

    std::uint64_t arena_small_vecs()
    {
        ct::Arena a(1 << 20);
        std::uint64_t acc = 0;
        for (int r = 0; r < 100; ++r)
        {
            for (int k = 0; k < 1000; ++k)
            {
                ct::Vector<int, ct::ArenaAlloc> v{ct::ArenaAlloc(a)};
                for (int i = 0; i < 16; ++i)
                    v.push_back(i + k);
                acc += static_cast<std::uint64_t>(v.back());
            }
            a.reset();
        }
        return acc;
    }

    std::uint64_t std_small_vecs()
    {
        std::uint64_t acc = 0;
        for (int r = 0; r < 100; ++r)
            for (int k = 0; k < 1000; ++k)
            {
                std::vector<int> v;
                for (int i = 0; i < 16; ++i)
                    v.push_back(i + k);
                acc += static_cast<std::uint64_t>(v.back());
            }
        return acc;
    }

} 

int main()
{
    std::printf("ct::Arena vs heap  (best of 7 runs, -O3 -march=native)\n");

    bench::header("alocação crua (coluna std = heap)");
    bench::compare("1M allocs de 64 B",
                   [] { bench::sink += arena_alloc_64(); },
                   [] { bench::sink += malloc_alloc_64(); });
    bench::compare("lista ligada 500k nós (create vs new)",
                   [] { bench::sink += arena_list(); },
                   [] { bench::sink += new_list(); });
    bench::compare("1000 frames x 1000 allocs + reset",
                   [] { bench::sink += arena_frames(); },
                   [] { bench::sink += malloc_frames(); });

    bench::header("Vector na arena");
    bench::compare("push_back 2M int (arena vs std)",
                   [] { bench::sink += arena_vec_push(); },
                   [] { bench::sink += std_vec_push(); });
    bench::compare("push_back 2M int (arena vs ct heap)",
                   [] { bench::sink += arena_vec_push(); },
                   [] { bench::sink += heap_vec_push(); });
    bench::compare("100k vectors pequenos (16 ints)",
                   [] { bench::sink += arena_small_vecs(); },
                   [] { bench::sink += std_small_vecs(); });

    return 0;
}