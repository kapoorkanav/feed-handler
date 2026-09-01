// Per-message book-update latency, measured with rdtsc and reported as
// percentiles. Only the book operation is timed: the symbol lookup happens
// before the first timestamp, so it is excluded from both versions equally.
//
// usage: bench_latency <itch-file> v1|v3
#include "common/itch_messages.hpp"
#include "common/itch_reader.hpp"
#include "v1/order_book.hpp"
#include "v3/order_book.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <unordered_map>
#include <vector>
#include <x86intrin.h>

namespace {

// lfence before the read stops earlier instructions drifting past it.
inline uint64_t tsc_start() {
    _mm_lfence();
    uint64_t t = __rdtsc();
    _mm_lfence();
    return t;
}

// rdtscp waits for prior instructions to retire; the trailing lfence stops
// later instructions being hoisted above it.
inline uint64_t tsc_end() {
    unsigned aux;
    uint64_t t = __rdtscp(&aux);
    _mm_lfence();
    return t;
}

double now_ns() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return double(ts.tv_sec) * 1e9 + double(ts.tv_nsec);
}

// Ticks per nanosecond, measured against CLOCK_MONOTONIC over ~200 ms.
double calibrate_tsc() {
    double a = now_ns();
    uint64_t t0 = tsc_start();
    while (now_ns() - a < 2e8) { }
    uint64_t t1 = tsc_end();
    double b = now_ns();
    return double(t1 - t0) / (b - a);
}

// Median cost of the two timestamp reads with nothing between them.
uint32_t timer_overhead() {
    std::vector<uint32_t> s;
    s.reserve(200000);
    for (int i = 0; i < 200000; ++i) {
        uint64_t a = tsc_start();
        uint64_t b = tsc_end();
        s.push_back(uint32_t(b - a));
    }
    std::sort(s.begin(), s.end());
    return s[s.size() / 2];
}

void report(const char* label, std::vector<uint32_t>& s,
            double ticks_per_ns, uint32_t overhead) {
    std::sort(s.begin(), s.end());
    auto pct = [&](double p) {
        size_t i = static_cast<size_t>(p * double(s.size() - 1));
        return double(s[i]) / ticks_per_ns;
    };
    std::printf("\n%s\n", label);
    std::printf("  samples        %zu\n", s.size());
    std::printf("  timer overhead %.1f ns (included in every figure below)\n",
                double(overhead) / ticks_per_ns);
    std::printf("  p50            %8.1f ns\n", pct(0.50));
    std::printf("  p90            %8.1f ns\n", pct(0.90));
    std::printf("  p99            %8.1f ns\n", pct(0.99));
    std::printf("  p99.9          %8.1f ns\n", pct(0.999));
    std::printf("  p99.99         %8.1f ns\n", pct(0.9999));
    std::printf("  max            %8.1f ns\n", double(s.back()) / ticks_per_ns);
}

constexpr size_t kPoolCapacity = 1u << 20;
constexpr size_t kRefTableCapacity = 1u << 18;
constexpr size_t kExpectedSamples = 8000000;

std::vector<uint32_t> run_v1(const char* path) {
    itch::ItchFileReader reader(path);
    std::unordered_map<uint16_t, feedhandler::OrderBook> books;
    std::vector<uint32_t> s;
    s.reserve(kExpectedSamples);

    const uint8_t* msg;
    uint16_t len;
    while (reader.next(msg, len)) {
        switch (msg[0]) {
            case 'A': {
                auto m = itch::AddOrder::parse(msg);
                auto& b = books[m.stock_locate];
                uint64_t t0 = tsc_start(); b.add_order(m); uint64_t t1 = tsc_end();
                s.push_back(uint32_t(t1 - t0));
                break;
            }
            case 'E': {
                auto m = itch::OrderExecuted::parse(msg);
                auto& b = books[m.stock_locate];
                uint64_t t0 = tsc_start(); b.execute_order(m); uint64_t t1 = tsc_end();
                s.push_back(uint32_t(t1 - t0));
                break;
            }
            case 'X': {
                auto m = itch::OrderCancel::parse(msg);
                auto& b = books[m.stock_locate];
                uint64_t t0 = tsc_start(); b.cancel_order(m); uint64_t t1 = tsc_end();
                s.push_back(uint32_t(t1 - t0));
                break;
            }
            case 'D': {
                auto m = itch::OrderDelete::parse(msg);
                auto& b = books[m.stock_locate];
                uint64_t t0 = tsc_start(); b.delete_order(m); uint64_t t1 = tsc_end();
                s.push_back(uint32_t(t1 - t0));
                break;
            }
            case 'U': {
                auto m = itch::OrderReplace::parse(msg);
                auto& b = books[m.stock_locate];
                uint64_t t0 = tsc_start(); b.replace_order(m); uint64_t t1 = tsc_end();
                s.push_back(uint32_t(t1 - t0));
                break;
            }
            default: break;
        }
    }
    return s;
}

std::vector<uint32_t> run_v3(const char* path) {
    itch::ItchFileReader reader(path);
    v3::OrderPool pool(kPoolCapacity);
    v3::RefTable refs(kRefTableCapacity);
    std::unordered_map<uint16_t, v3::OrderBook> books;
    std::vector<uint32_t> s;
    s.reserve(kExpectedSamples);

    const uint8_t* msg;
    uint16_t len;
    while (reader.next(msg, len)) {
        switch (msg[0]) {
            case 'A': {
                auto m = itch::AddOrder::parse(msg);
                auto& b = books.try_emplace(m.stock_locate, pool, refs).first->second;
                uint64_t t0 = tsc_start(); b.add_order(m); uint64_t t1 = tsc_end();
                s.push_back(uint32_t(t1 - t0));
                break;
            }
            case 'E': {
                auto m = itch::OrderExecuted::parse(msg);
                auto& b = books.try_emplace(m.stock_locate, pool, refs).first->second;
                uint64_t t0 = tsc_start(); b.execute_order(m); uint64_t t1 = tsc_end();
                s.push_back(uint32_t(t1 - t0));
                break;
            }
            case 'X': {
                auto m = itch::OrderCancel::parse(msg);
                auto& b = books.try_emplace(m.stock_locate, pool, refs).first->second;
                uint64_t t0 = tsc_start(); b.cancel_order(m); uint64_t t1 = tsc_end();
                s.push_back(uint32_t(t1 - t0));
                break;
            }
            case 'D': {
                auto m = itch::OrderDelete::parse(msg);
                auto& b = books.try_emplace(m.stock_locate, pool, refs).first->second;
                uint64_t t0 = tsc_start(); b.delete_order(m); uint64_t t1 = tsc_end();
                s.push_back(uint32_t(t1 - t0));
                break;
            }
            case 'U': {
                auto m = itch::OrderReplace::parse(msg);
                auto& b = books.try_emplace(m.stock_locate, pool, refs).first->second;
                uint64_t t0 = tsc_start(); b.replace_order(m); uint64_t t1 = tsc_end();
                s.push_back(uint32_t(t1 - t0));
                break;
            }
            default: break;
        }
    }
    return s;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: %s <itch-file> v1|v3\n", argv[0]);
        return 1;
    }

    double ticks_per_ns = calibrate_tsc();
    uint32_t overhead = timer_overhead();
    std::printf("tsc %.3f GHz, timer overhead %u ticks (%.1f ns)\n",
                ticks_per_ns, overhead, double(overhead) / ticks_per_ns);

    std::vector<uint32_t> samples;
    if (std::strcmp(argv[2], "v1") == 0) {
        samples = run_v1(argv[1]);
        report("v1 book update", samples, ticks_per_ns, overhead);
    } else if (std::strcmp(argv[2], "v3") == 0) {
        samples = run_v3(argv[1]);
        report("v3 book update", samples, ticks_per_ns, overhead);
    } else {
        std::fprintf(stderr, "unknown version '%s'\n", argv[2]);
        return 1;
    }
    return 0;
}
