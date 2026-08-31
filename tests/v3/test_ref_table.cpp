// Stress test: RefTable must behave identically to std::unordered_map under a
// random mix of insert/find/erase. Backward-shift deletion is easy to get
// subtly wrong, and a bug would silently lose orders rather than crash.
#include "v3/ref_table.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <random>
#include <unordered_map>
#include <vector>

int main() {
    constexpr size_t kCapacity = 1u << 12;   // small, to force heavy probing
    constexpr int kIterations = 2000000;

    v3::RefTable table(kCapacity);
    std::unordered_map<uint64_t, uint32_t> oracle;

    std::mt19937_64 rng(12345);
    std::vector<uint64_t> live;

    // Keep load factor well under 0.5; small key space forces real collisions.
    const size_t kMaxLive = kCapacity / 4;
    const uint64_t kKeySpace = kCapacity * 3;

    for (int i = 0; i < kIterations; ++i) {
        int action = rng() % 100;

        if (action < 45 && live.size() < kMaxLive) {
            uint64_t key = rng() % kKeySpace;
            uint32_t val = static_cast<uint32_t>(rng() & 0xFFFFFF);
            bool was_new = oracle.find(key) == oracle.end();
            table.insert(key, val);
            oracle[key] = val;
            if (was_new) live.push_back(key);
        } else if (action < 75 && !live.empty()) {
            size_t pos = rng() % live.size();
            uint64_t key = live[pos];
            table.erase(key);
            oracle.erase(key);
            live[pos] = live.back();
            live.pop_back();
        } else {
            uint64_t key = rng() % kKeySpace;
            uint32_t got = table.find(key);
            auto it = oracle.find(key);
            if (it == oracle.end()) {
                if (got != v3::kNullIdx) {
                    std::printf("FAIL iter=%d key=%llu: table has %u, oracle has none\n",
                                i, (unsigned long long)key, got);
                    return 1;
                }
            } else {
                if (got != it->second) {
                    std::printf("FAIL iter=%d key=%llu: table=%u oracle=%u\n",
                                i, (unsigned long long)key, got, it->second);
                    return 1;
                }
            }
        }

        if (table.size() != oracle.size()) {
            std::printf("FAIL iter=%d: size mismatch table=%zu oracle=%zu\n",
                        i, table.size(), oracle.size());
            return 1;
        }
    }

    // Final sweep: every key the oracle knows must be findable, and a sample of
    // absent keys must not be.
    for (const auto& kv : oracle) {
        if (table.find(kv.first) != kv.second) {
            std::printf("FAIL final sweep: key=%llu missing\n",
                        (unsigned long long)kv.first);
            return 1;
        }
    }
    for (uint64_t k = 0; k < kKeySpace; ++k) {
        if (oracle.find(k) == oracle.end() && table.find(k) != v3::kNullIdx) {
            std::printf("FAIL final sweep: key=%llu should be absent\n",
                        (unsigned long long)k);
            return 1;
        }
    }

    std::printf("ref table matches std::unordered_map over %d ops (final size=%zu)\n",
                kIterations, table.size());
    return 0;
}
