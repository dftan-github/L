// "Free-for-all" C++ benchmark (-O3): every trick allowed, fastest wins.
//
//   A string concat   1e6 short appends into one reserved std::string
//   B small objects   1e6 allocations from a custom bump arena
//   C float math      1e7 sin/cos evaluations accumulated
//
// The strict-semantics suite lives in bench_cpp.cpp; this file is the
// "squeeze everything" counterpart of bench_freeforall_l.l.

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace {

using Clock = std::chrono::high_resolution_clock;

double ms_since(Clock::time_point t0) {
    const auto t1 = Clock::now();
    return std::chrono::duration<double, std::milli>(t1 - t0).count();
}

// ---------------------------------------------------------------------------
// A. String concatenation: 1e6 short appends. One reserve serves the whole run
// (no reallocation), and append is the cheapest per-call growth path.
// ---------------------------------------------------------------------------
std::size_t bench_strings() {
    std::string out;
    out.reserve(4u * 1024u * 1024u);  // 4 MiB: enough for 1e6 x "k<i>" pieces
    for (std::int32_t i = 0; i < 1000000; ++i) {
        out.append("k");
        out.append(std::to_string(i));
        out.append(",");
    }
    return out.size();
}

// ---------------------------------------------------------------------------
// B. Small-object allocation: a hand-rolled bump allocator. One 32 MiB block,
// pointer bump per object, single free at the end — the C++ twin of L's arena.
// ---------------------------------------------------------------------------
struct Point {
    std::int32_t x;
    std::int32_t y;
};

class BumpArena {
public:
    explicit BumpArena(std::size_t bytes) : base_(new unsigned char[bytes]), cap_(bytes) {}
    ~BumpArena() { delete[] base_; }
    BumpArena(const BumpArena&) = delete;
    BumpArena& operator=(const BumpArena&) = delete;

    template <typename T, typename... Args>
    T* make(Args&&... args) {
        void* slot = base_ + used_;
        used_ += sizeof(T);
        return new (slot) T{std::forward<Args>(args)...};
    }

private:
    unsigned char* base_;
    std::size_t used_ = 0;
    std::size_t cap_;
};

std::int64_t bench_objects() {
    std::int64_t acc = 0;
    BumpArena arena(32u * 1024u * 1024u);
    for (std::int32_t i = 0; i < 1000000; ++i) {
        Point* p = arena.make<Point>(i, i + 1);
        acc = acc + p->x + p->y;
    }
    return acc;
}

// ---------------------------------------------------------------------------
// C. Float math: 1e7 sin/cos evaluations. Calling the same libm the L side
// links keeps the comparison honest — this measures the loop, not the libm.
// ---------------------------------------------------------------------------
double bench_math() {
    double s = 0.0;
    for (std::int32_t i = 0; i < 10000000; ++i) {
        const double t = static_cast<double>(i) * 0.001;
        s = s + std::sin(t) * std::cos(t);
    }
    return s;
}

}  // namespace

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    auto t0 = Clock::now();
    const std::size_t a = bench_strings();
    std::printf("A strings : %.3f ms\n", ms_since(t0));
    std::printf("  bytes=%zu\n", a);

    auto t1 = Clock::now();
    const std::int64_t b = bench_objects();
    std::printf("B objects : %.3f ms\n", ms_since(t1));
    std::printf("  acc=%lld\n", static_cast<long long>(b));

    auto t2 = Clock::now();
    const double c = bench_math();
    std::printf("C math    : %.3f ms\n", ms_since(t2));
    std::printf("  sum=%.6g\n", c);

    std::printf("C++ total : %.3f ms\n", ms_since(t0));
    return 0;
}
