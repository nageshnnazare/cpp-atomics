// ch12 — measure the cost of false sharing.
// build: clang++ -std=c++20 -O2 -pthread ch12_false_sharing.cpp -o /tmp/d && /tmp/d
#include <atomic>
#include <thread>
#include <vector>
#include <chrono>
#include <iostream>
#include <new>

constexpr int THREADS = 4;
constexpr long ITERS  = 50'000'000;

struct Shared {                 // all counters in ONE cache line -> false sharing
    std::atomic<long> c[THREADS];
};

struct Padded {                 // each counter on its own cache line
    struct alignas(std::hardware_destructive_interference_size) Cell {
        std::atomic<long> v{0};
    };
    Cell c[THREADS];
};

template <class Arr>
double bench(Arr& arr) {
    auto get = [&](int i) -> std::atomic<long>& {
        if constexpr (requires { arr.c[i].v; }) return arr.c[i].v;
        else                                    return arr.c[i];
    };
    auto start = std::chrono::steady_clock::now();
    std::vector<std::thread> ts;
    for (int i = 0; i < THREADS; ++i)
        ts.emplace_back([&, i] {
            for (long j = 0; j < ITERS; ++j)
                get(i).fetch_add(1, std::memory_order_relaxed);
        });
    for (auto& t : ts) t.join();
    auto end = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(end - start).count();
}

int main() {
    Shared shared{};
    Padded padded{};
    double t_false = bench(shared);
    double t_pad   = bench(padded);
    std::cout << "false sharing : " << t_false << " s\n";
    std::cout << "padded        : " << t_pad   << " s\n";
    std::cout << "speedup       : " << (t_false / t_pad) << "x\n";
}
