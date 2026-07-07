// ch01 — demonstrates a DATA RACE and its fix.
// Run the racy version under ThreadSanitizer to SEE the race:
//   clang++ -std=c++20 -O1 -g -fsanitize=thread -pthread ch01_race.cpp -o /tmp/race && /tmp/race
// Plain build shows the wrong (random) count:
//   clang++ -std=c++20 -O2 -pthread ch01_race.cpp -o /tmp/race && /tmp/race
#include <atomic>
#include <thread>
#include <vector>
#include <iostream>

static long        racy_counter   = 0;    // plain long: DATA RACE
static std::atomic<long> safe_counter{0}; // atomic: correct

constexpr int PER_THREAD = 100000;
constexpr int THREADS    = 8;

void racy()  { for (int i = 0; i < PER_THREAD; ++i) ++racy_counter; }        // UB
void safe()  { for (int i = 0; i < PER_THREAD; ++i)
                   safe_counter.fetch_add(1, std::memory_order_relaxed); }   // OK

int main() {
    {
        std::vector<std::thread> ts;
        for (int i = 0; i < THREADS; ++i) ts.emplace_back(racy);
        for (auto& t : ts) t.join();
    }
    {
        std::vector<std::thread> ts;
        for (int i = 0; i < THREADS; ++i) ts.emplace_back(safe);
        for (auto& t : ts) t.join();
    }
    const long expected = static_cast<long>(THREADS) * PER_THREAD;
    std::cout << "expected     : " << expected      << '\n';
    std::cout << "racy_counter : " << racy_counter  << "  (usually < expected!)\n";
    std::cout << "safe_counter : " << safe_counter.load() << "  (always exact)\n";
}
