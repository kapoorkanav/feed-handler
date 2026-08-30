// Throwaway analysis tool: profiles the real ITCH sample to size the v3
// price-level array and object pool. Not part of the feed handler itself.
#include "common/itch_messages.hpp"
#include "common/itch_reader.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s <itch-file>\n", argv[0]);
        return 1;
    }

    itch::ItchFileReader reader(argv[1]);

    struct SymStats {
        uint32_t min_price = UINT32_MAX;
        uint32_t max_price = 0;
        std::unordered_set<uint32_t> distinct_prices;
    };
    std::unordered_map<uint16_t, SymStats> stats;

    std::unordered_map<uint64_t, uint16_t> live_orders;  // ref -> symbol
    size_t max_live = 0;

    uint64_t total_adds = 0;
    uint64_t not_multiple_of_1000 = 0;  // sub-penny prices
    uint64_t not_multiple_of_100 = 0;

    const uint8_t* msg;
    uint16_t len;
    while (reader.next(msg, len)) {
        switch (msg[0]) {
            case 'A': {
                auto m = itch::AddOrder::parse(msg);
                auto& s = stats[m.stock_locate];
                s.min_price = std::min(s.min_price, m.price);
                s.max_price = std::max(s.max_price, m.price);
                s.distinct_prices.insert(m.price);
                live_orders[m.order_ref] = m.stock_locate;
                max_live = std::max(max_live, live_orders.size());
                total_adds++;
                if (m.price % 1000 != 0) not_multiple_of_1000++;
                if (m.price % 100 != 0) not_multiple_of_100++;
                break;
            }
            case 'D': {
                auto m = itch::OrderDelete::parse(msg);
                live_orders.erase(m.order_ref);
                break;
            }
            case 'U': {
                auto m = itch::OrderReplace::parse(msg);
                live_orders.erase(m.original_order_ref);
                live_orders[m.new_order_ref] = 0;
                break;
            }
            default:
                break;
        }
    }

    // Second pass: how often would a windowed cent-indexed array actually hit?
    // Window is centred on the first penny-aligned price seen for each
    // symbol+side; sub-penny prices can't be cent-indexed without collisions,
    // so they always count as overflow.
    {
        const uint32_t windows[] = {256, 1024, 4096, 16384};
        for (uint32_t W : windows) {
            itch::ItchFileReader r2(argv[1]);
            std::unordered_map<uint32_t, int64_t> base;  // (locate<<1|side) -> base cent
            uint64_t in_window = 0, overflow = 0, subpenny = 0;

            const uint8_t* m2;
            uint16_t l2;
            while (r2.next(m2, l2)) {
                if (m2[0] != 'A') continue;
                auto m = itch::AddOrder::parse(m2);
                if (m.price % 100 != 0) { subpenny++; overflow++; continue; }

                int64_t cents = m.price / 100;
                uint32_t key = (static_cast<uint32_t>(m.stock_locate) << 1) |
                               (m.side == 'B' ? 0u : 1u);
                auto it = base.find(key);
                if (it == base.end()) {
                    base[key] = cents - W / 2;
                    in_window++;
                    continue;
                }
                int64_t off = cents - it->second;
                if (off >= 0 && off < static_cast<int64_t>(W)) in_window++;
                else overflow++;
            }
            uint64_t tot = in_window + overflow;
            std::printf("window=%-6u  in-array=%.3f%%  overflow=%.3f%%  (sub-penny=%.3f%%)\n",
                        W, 100.0 * in_window / tot, 100.0 * overflow / tot,
                        100.0 * subpenny / tot);
        }
    }

    std::vector<size_t> level_counts;
    std::vector<uint64_t> price_ranges;
    for (auto& kv : stats) {
        if (kv.second.max_price == 0) continue;
        level_counts.push_back(kv.second.distinct_prices.size());
        price_ranges.push_back(
            static_cast<uint64_t>(kv.second.max_price) - kv.second.min_price);
    }
    std::sort(level_counts.begin(), level_counts.end());
    std::sort(price_ranges.begin(), price_ranges.end());

    auto pct = [](std::vector<size_t>& v, double p) -> size_t {
        if (v.empty()) return 0;
        size_t i = static_cast<size_t>(p * (v.size() - 1));
        return v[i];
    };
    auto pct64 = [](std::vector<uint64_t>& v, double p) -> uint64_t {
        if (v.empty()) return 0;
        size_t i = static_cast<size_t>(p * (v.size() - 1));
        return v[i];
    };

    std::printf("symbols with orders:        %zu\n", level_counts.size());
    std::printf("total add-order messages:   %llu\n", (unsigned long long)total_adds);
    std::printf("peak live orders (approx):  %zu\n", max_live);
    std::printf("\n-- tick granularity --\n");
    std::printf("prices not multiple of 1000 (sub-penny): %llu (%.2f%%)\n",
                (unsigned long long)not_multiple_of_1000,
                100.0 * not_multiple_of_1000 / total_adds);
    std::printf("prices not multiple of 100:              %llu (%.2f%%)\n",
                (unsigned long long)not_multiple_of_100,
                100.0 * not_multiple_of_100 / total_adds);

    std::printf("\n-- distinct price levels per symbol --\n");
    std::printf("p50=%zu  p90=%zu  p99=%zu  max=%zu\n",
                pct(level_counts, 0.50), pct(level_counts, 0.90),
                pct(level_counts, 0.99), level_counts.back());

    std::printf("\n-- price range per symbol (max-min, ITCH units) --\n");
    std::printf("p50=%llu  p90=%llu  p99=%llu  max=%llu\n",
                (unsigned long long)pct64(price_ranges, 0.50),
                (unsigned long long)pct64(price_ranges, 0.90),
                (unsigned long long)pct64(price_ranges, 0.99),
                (unsigned long long)price_ranges.back());

    return 0;
}
