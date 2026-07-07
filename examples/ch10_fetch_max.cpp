// ch10 — implement atomic fetch_max with a CAS loop (no built-in for it).
// build: clang++ -std=c++20 -O2 -pthread ch10_fetch_max.cpp -o /tmp/d && /tmp/d
#include <atomic>
#include <thread>
#include <vector>
#include <iostream>

int atomic_fetch_max(std::atomic<int>& a, int v) {
    int cur = a.load(std::memory_order_relaxed);
    while (v > cur &&
           !a.compare_exchange_weak(cur, v, std::memory_order_relaxed)) {
        // cur refreshed by failed CAS; loop re-checks v > cur
    }
    return cur;
}

int main() {
    std::atomic<int> peak{0};
    std::vector<std::thread> ts;
    for (int i = 0; i < 8; ++i)
        ts.emplace_back([&, i] {
            for (int j = 0; j < 100000; ++j)
                atomic_fetch_max(peak, (i * 100000 + j) % 777777);
        });
    for (auto& t : ts) t.join();
    std::cout << "peak observed = " << peak.load() << '\n';
}
