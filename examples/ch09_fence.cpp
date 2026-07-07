// ch09 — fence-based message passing (release fence + relaxed store).
// build: clang++ -std=c++20 -O2 -pthread ch09_fence.cpp -o /tmp/d && /tmp/d
#include <atomic>
#include <thread>
#include <cassert>
#include <iostream>

int a = 0, b = 0, c = 0;             // plain data
std::atomic<bool> ready{false};

void producer() {
    a = 1; b = 2; c = 3;                                    // relaxed writes
    std::atomic_thread_fence(std::memory_order_release);    // one barrier
    ready.store(true, std::memory_order_relaxed);           // relaxed store
}

void consumer() {
    while (!ready.load(std::memory_order_relaxed))          // relaxed load
        std::this_thread::yield();
    std::atomic_thread_fence(std::memory_order_acquire);    // one barrier
    assert(a == 1 && b == 2 && c == 3);                     // all visible
}

int main() {
    for (int i = 0; i < 1000; ++i) {
        a = b = c = 0; ready.store(false);
        std::thread cs(consumer), pr(producer);
        pr.join(); cs.join();
    }
    std::cout << "fence-based publish OK for 1000 trials\n";
}
