// ch03 — a correct shared counter with atomic RMW
// build: clang++ -std=c++20 -O2 -pthread ch03_counter.cpp -o /tmp/d && /tmp/d
#include <atomic>
#include <thread>
#include <vector>
#include <iostream>

std::atomic<long> counter{0};

void work() {
    for (int i = 0; i < 100000; ++i)
        counter.fetch_add(1, std::memory_order_relaxed);
}

int main() {
    std::vector<std::thread> ts;
    for (int i = 0; i < 8; ++i) ts.emplace_back(work);
    for (auto& t : ts) t.join();
    std::cout << counter.load() << '\n';   // 800000
    std::cout << "atomic<long> always lock-free: "
              << std::atomic<long>::is_always_lock_free << '\n';
}
