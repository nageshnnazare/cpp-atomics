// ch12 — a spinlock built from atomic_flag (test-and-test-and-set).
// build: clang++ -std=c++20 -O2 -pthread ch12_spinlock.cpp -o /tmp/d && /tmp/d
#include <atomic>
#include <thread>
#include <vector>
#include <iostream>

class SpinLock {
    std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
public:
    void lock() {
        while (flag_.test_and_set(std::memory_order_acquire)) {
#if defined(__cpp_lib_atomic_flag_test)
            while (flag_.test(std::memory_order_relaxed))   // read-only spin
#endif
                std::this_thread::yield();
        }
    }
    void unlock() { flag_.clear(std::memory_order_release); }
};

SpinLock lock;
long guarded = 0;   // protected by the spinlock (plain long is fine inside)

int main() {
    constexpr int THREADS = 8, PER = 100000;
    std::vector<std::thread> ts;
    for (int i = 0; i < THREADS; ++i)
        ts.emplace_back([] {
            for (int j = 0; j < PER; ++j) {
                lock.lock();
                ++guarded;          // critical section
                lock.unlock();
            }
        });
    for (auto& t : ts) t.join();
    std::cout << "guarded = " << guarded
              << " (expected " << (long)THREADS * PER << ")\n";
}
