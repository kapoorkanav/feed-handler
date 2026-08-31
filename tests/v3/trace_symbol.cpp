// Debug tool: replay every message for ONE stock_locate through both books,
// printing top-of-book after each, and stop at the first divergence.
#include "common/itch_messages.hpp"
#include "common/itch_reader.hpp"
#include "v1/order_book.hpp"
#include "v3/order_book.hpp"

#include <cstdio>
#include <cstdint>
#include <cstdlib>

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: %s <itch-file> <stock_locate>\n", argv[0]);
        return 1;
    }
    const uint16_t want = static_cast<uint16_t>(std::atoi(argv[2]));

    itch::ItchFileReader reader(argv[1]);
    feedhandler::OrderBook v1;
    v3::OrderPool pool(1u << 20);
    v3::RefTable refs(1u << 18);
    v3::OrderBook v3b(pool, refs);

    uint64_t n = 0, shown = 0;
    const uint8_t* msg;
    uint16_t len;

    while (reader.next(msg, len)) {
        ++n;
        uint16_t locate = 0;
        bool touched = false;
        char what[128] = {0};

        switch (msg[0]) {
            case 'A': {
                auto m = itch::AddOrder::parse(msg);
                if ((locate = m.stock_locate) != want) break;
                std::snprintf(what, sizeof(what), "ADD ref=%llu %c px=%u sh=%u",
                              (unsigned long long)m.order_ref, m.side, m.price, m.shares);
                v1.add_order(m);
                v3b.add_order(m);
                touched = true;
                break;
            }
            case 'E': {
                auto m = itch::OrderExecuted::parse(msg);
                if ((locate = m.stock_locate) != want) break;
                std::snprintf(what, sizeof(what), "EXEC ref=%llu sh=%u",
                              (unsigned long long)m.order_ref, m.executed_shares);
                v1.execute_order(m);
                v3b.execute_order(m);
                touched = true;
                break;
            }
            case 'X': {
                auto m = itch::OrderCancel::parse(msg);
                if ((locate = m.stock_locate) != want) break;
                std::snprintf(what, sizeof(what), "CANCEL ref=%llu sh=%u",
                              (unsigned long long)m.order_ref, m.canceled_shares);
                v1.cancel_order(m);
                v3b.cancel_order(m);
                touched = true;
                break;
            }
            case 'D': {
                auto m = itch::OrderDelete::parse(msg);
                if ((locate = m.stock_locate) != want) break;
                std::snprintf(what, sizeof(what), "DELETE ref=%llu",
                              (unsigned long long)m.order_ref);
                v1.delete_order(m);
                v3b.delete_order(m);
                touched = true;
                break;
            }
            case 'U': {
                auto m = itch::OrderReplace::parse(msg);
                if ((locate = m.stock_locate) != want) break;
                std::snprintf(what, sizeof(what), "REPLACE old=%llu new=%llu px=%u sh=%u",
                              (unsigned long long)m.original_order_ref,
                              (unsigned long long)m.new_order_ref, m.price, m.shares);
                v1.replace_order(m);
                v3b.replace_order(m);
                touched = true;
                break;
            }
            default:
                break;
        }

        if (!touched) continue;

        auto a = v1.best_bid_ask();
        auto b = v3b.best_bid_ask();
        bool bad = (a != b);

        if (shown < 60 || bad) {
            std::printf("#%-9llu %-46s | v1: ", (unsigned long long)n, what);
            if (a) std::printf("%u/%u", a->first, a->second); else std::printf("none ");
            std::printf("   v3: ");
            if (b) std::printf("%u/%u", b->first, b->second); else std::printf("none ");
            if (bad) std::printf("   <<< MISMATCH");
            std::printf("\n");
            ++shown;
        }
        if (bad) {
            std::printf("\nstopping at first divergence\n");
            return 1;
        }
    }
    std::printf("\nno divergence for locate=%u\n", want);
    return 0;
}
