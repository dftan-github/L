// C++ side of the L vs C++ benchmark suite (-O3).
// Workloads mirror benchmarks/bench_l.l one-to-one; every section prints ms.
//
//   A dense float arithmetic   1e8 iterations of sum += i * 2.5
//   B object create + release  1e6 `new Point` objects, deleted each iteration
//   C string map               1e6 short keys inserted, then 1e6 lookups

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

// A. Dense float arithmetic — 1e8 iterations. Float addition is not
// reassociable, so this survives -O3 as a real loop, exactly like the L twin.
double bench_arith() {
    double sum = 0.0;
    for (std::int32_t i = 0; i < 100000000; ++i) {
        sum = sum + i * 2.5;
    }
    return sum;
}

// B. Object create + release — 1e6 two-field objects, each allocated with `new`
// and released with `delete` (the C++ counterpart of L's `heap Point` + auto-free).
// `p` is passed through an opaque `sink` so the allocator cannot prove the pair
// dead and elide it (without that barrier this loop compiles to nothing at -O3).
volatile std::int64_t sink_b = 0;

std::int64_t bench_objects() {
    std::int64_t acc = 0;
    for (std::int32_t i = 0; i < 1000000; ++i) {
        Point* p = new Point{i, i + 1};
        acc = acc + p->x + p->y;
        sink_b = acc;
        delete p;
    }
    return acc;
}

// C. String map — build 1e6 short keys, insert every one, then look up every one.
std::int32_t bench_strings() {
    std::vector<std::string> keys;
    keys.reserve(1000000);
    for (std::int32_t i = 0; i < 1000000; ++i) {
        keys.push_back("k" + std::to_string(i));
    }
    std::unordered_map<std::string, std::int32_t> m;
    m.reserve(1048576);
    for (std::int32_t i = 0; i < 1000000; ++i) {
        m[keys[static_cast<std::size_t>(i)]] = i;
    }
    // Lookups: sum the hits so the reads cannot be dropped.
    std::int64_t hits = 0;
    for (std::int32_t i = 0; i < 1000000; ++i) {
        auto it = m.find(keys[static_cast<std::size_t>(i)]);
        if (it != m.end()) {
            ++hits;
        }
    }
    std::printf("C lookups hit: %lld\n", static_cast<long long>(hits));
    return static_cast<std::int32_t>(m.size());
}

}  // namespace

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    auto t0 = Clock::now();
    const double a = bench_arith();
    std::printf("A arith   : %.3f ms\n", ms_since(t0));
    std::printf("  sum=%.6g\n", a);

    auto t1 = Clock::now();
    const std::int64_t b = bench_objects();
    std::printf("B objects : %.3f ms\n", ms_since(t1));
    std::printf("  acc=%lld\n", static_cast<long long>(b));

    auto t2 = Clock::now();
    const std::int32_t c = bench_strings();
    std::printf("C strings : %.3f ms\n", ms_since(t2));
    std::printf("  map_size=%d\n", c);

    std::printf("C++ total : %.3f ms\n", ms_since(t0));
    return 0;
}
