// ch11 — single-producer single-consumer lock-free ring buffer.
// build: clang++ -std=c++20 -O2 -pthread ch11_spsc_queue.cpp -o /tmp/d && /tmp/d
// Try under TSan (should be clean):
//   clang++ -std=c++20 -O1 -g -fsanitize=thread -pthread ch11_spsc_queue.cpp -o /tmp/d && /tmp/d
#include <atomic>
#include <vector>
#include <optional>
#include <thread>
#include <iostream>
#include <new>

template <class T>
class SpscQueue {
    std::vector<T> buf_;
    std::size_t    cap_;
    alignas(std::hardware_destructive_interference_size) std::atomic<std::size_t> head_{0};
    alignas(std::hardware_destructive_interference_size) std::atomic<std::size_t> tail_{0};
public:
    explicit SpscQueue(std::size_t cap) : buf_(cap + 1), cap_(cap + 1) {}

    bool push(const T& item) {
        const auto tail = tail_.load(std::memory_order_relaxed);
        const auto next = (tail + 1) % cap_;
        if (next == head_.load(std::memory_order_acquire)) return false; // full
        buf_[tail] = item;
        tail_.store(next, std::memory_order_release);
        return true;
    }
    std::optional<T> pop() {
        const auto head = head_.load(std::memory_order_relaxed);
        if (head == tail_.load(std::memory_order_acquire)) return std::nullopt; // empty
        T item = buf_[head];
        head_.store((head + 1) % cap_, std::memory_order_release);
        return item;
    }
};

int main() {
    SpscQueue<int> q(1024);
    constexpr int N = 1000000;
    long long sum = 0;

    std::thread producer([&] {
        for (int i = 1; i <= N; ++i)
            while (!q.push(i)) std::this_thread::yield();
    });
    std::thread consumer([&] {
        int received = 0;
        while (received < N) {
            if (auto v = q.pop()) { sum += *v; ++received; }
            else std::this_thread::yield();
        }
    });
    producer.join();
    consumer.join();

    long long expected = 1LL * N * (N + 1) / 2;
    std::cout << "sum=" << sum << " expected=" << expected
              << (sum == expected ? " OK\n" : " MISMATCH\n");
}
