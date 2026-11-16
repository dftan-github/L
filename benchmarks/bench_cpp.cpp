// C++ side of the L vs C++ benchmark suite (-O3).
// Workloads mirror benchmarks/bench_l.l one-to-one; every section prints ms.
//
//   A dense arithmetic loop      1e8 float accumulations (non-foldable shape)
//   B heap object creation       1e7 Point{i32,i32} objects over 10 batch blocks
//   C string-keyed map           1e6 puts over a fixed 1e4-key pool

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

struct Point {
    std::int32_t x;
    std::int32_t y;
};

using Clock = std::chrono::high_resolution_clock;

double ms_since(Clock::time_point t0) {
    const auto t1 = Clock::now();
    return std::chrono::duration<double, std::milli>(t1 - t0).count();
}

// A. Dense arithmetic — 1e8 iterations of a float accumulation. Float addition is
// not reassociable, so LLVM cannot derive a closed form and the loop genuinely
// runs (an integer `sum += i*2+1` would be folded into one polynomial at -O3 on
// both compilers). The L twin uses the exact same expression.
double bench_arith() {
    double sum = 0.0;
    for (std::int32_t i = 0; i < 100000000; ++i) {
        sum = sum + i * 0.5;
    }
    return sum;
}

// B. Heap object creation — 10 batches of 1e6 two-field objects. Each batch comes
// from one bump-allocated block (matching L's `arena(16777216)`: one allocation
// serves the whole batch, released once). 1e7 objects total.
std::int64_t bench_objects() {
    std::int64_t acc = 0;
    for (std::int32_t batch = 0; batch < 10; ++batch) {
        std::vector<Point> block(1000000);
        for (std::int32_t i = 0; i < 1000000; ++i) {
            Point* p = &block[static_cast<std::size_t>(i)];
            p->x = i;
            p->y = i + 1;
            acc = acc + p->x + p->y;
        }
    }
    return acc;
}

// C. String hashing — keyed map inserts over a fixed 1e4-key pool. Keys are built
// once (excluded from timing), then 1e6 puts (100 rounds x 1e4) measure the map's
// hash + probe path, not string construction. Mirrors the L twin.
std::int32_t bench_strings() {
    std::vector<std::string> keys;
    keys.reserve(10000);
    for (std::int32_t i = 0; i < 10000; ++i) {
        keys.push_back("key_" + std::to_string(i));
    }
    std::unordered_map<std::string, std::int32_t> m;
    m.reserve(16384);
    for (std::int32_t round = 0; round < 100; ++round) {
        for (std::int32_t j = 0; j < 10000; ++j) {
            m[keys[static_cast<std::size_t>(j)]] = j;
        }
    }
    return static_cast<std::int32_t>(m.size());
}

}  // namespace

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    auto t0 = Clock::now();
    const double a = bench_arith();
    std::printf("A arith      : %.3f ms (sum=%.6g)\n", ms_since(t0), a);

    auto t1 = Clock::now();
    const std::int64_t b = bench_objects();
    std::printf("B objects    : %.3f ms (acc=%lld)\n", ms_since(t1),
                static_cast<long long>(b));

    auto t2 = Clock::now();
    const std::int32_t c = bench_strings();
    std::printf("C strings    : %.3f ms (map_size=%d)\n", ms_since(t2), c);

    std::printf("C++ total    : %.3f ms\n", ms_since(t0));
    return 0;
}
