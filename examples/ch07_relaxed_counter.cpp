// ch07 — relaxed is correct for a value-only counter aggregated after join.
// build: clang++ -std=c++20 -O2 -pthread ch07_relaxed_counter.cpp -o /tmp/d && /tmp/d
#include <atomic>
#include <thread>
#include <vector>
#include <iostream>

std::atomic<unsigned long> hits{0};
std::atomic<unsigned>      next_id{0};

int main() {
    constexpr int THREADS = 8, PER = 200000;

    std::vector<std::thread> ts;
    for (int i = 0; i < THREADS; ++i)
        ts.emplace_back([] {
            for (int j = 0; j < PER; ++j)
                hits.fetch_add(1, std::memory_order_relaxed);
        });
    for (auto& t : ts) t.join();     // join() establishes happens-before

    std::cout << "hits = " << hits.load(std::memory_order_relaxed)
              << " (expected " << (unsigned long)THREADS * PER << ")\n";

    // unique-id allocator: relaxed fetch_add hands out unique values
    std::atomic<int> collisions{0};
    std::vector<std::thread> t2;
    std::vector<unsigned> ids(THREADS);
    for (int i = 0; i < THREADS; ++i)
        t2.emplace_back([&, i] { ids[i] = next_id.fetch_add(1, std::memory_order_relaxed); });
    for (auto& t : t2) t.join();
    std::cout << "allocated " << next_id.load() << " unique ids\n";
    (void)collisions;
}
