// ch13 — std::atomic_ref: atomic operations over a plain (non-atomic) object.
// build: clang++ -std=c++20 -O2 -pthread ch13_atomic_ref.cpp -o /tmp/d && /tmp/d
#include <atomic>
#include <thread>
#include <vector>
#include <numeric>
#include <iostream>

int main() {
    // A plain array. We want atomic increments on ONE shared element while all
    // concurrent accesses to it go through atomic_ref.
    std::vector<int> data(16, 0);
    const std::size_t shared_index = 7;

    constexpr int THREADS = 8, PER = 100000;
    std::vector<std::thread> ts;
    for (int i = 0; i < THREADS; ++i)
        ts.emplace_back([&] {
            std::atomic_ref<int> ref(data[shared_index]);   // atomic view
            for (int j = 0; j < PER; ++j)
                ref.fetch_add(1, std::memory_order_relaxed);
        });
    for (auto& t : ts) t.join();

    std::cout << "data[" << shared_index << "] = " << data[shared_index]
              << " (expected " << THREADS * PER << ")\n";
    std::cout << "atomic_ref<int>::required_alignment = "
              << std::atomic_ref<int>::required_alignment << '\n';
    std::cout << "is_always_lock_free = "
              << std::atomic_ref<int>::is_always_lock_free << '\n';
}
