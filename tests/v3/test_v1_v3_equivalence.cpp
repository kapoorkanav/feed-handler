// Replays the real ITCH sample through the v1 and v3 books side by side and
// asserts they agree on top-of-book after every single message. A faster book
// that disagrees with the baseline is worthless, so this is the gate v3 must
// pass before any performance claim about it means anything.
#include "common/itch_messages.hpp"
#include "common/itch_reader.hpp"
#include "v1/order_book.hpp"
#include "v3/order_book.hpp"

#include <cstdio>
#include <cstdint>
#include <unordered_map>

namespace {

constexpr size_t kPoolCapacity = 1u << 20;   // 1M orders; measured peak was ~76k
constexpr uint64_t kMaxMismatchesToPrint = 10;

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s <itch-file>\n", argv[0]);
        return 1;
    }

    itch::ItchFileReader reader(argv[1]);

    std::unordered_map<uint16_t, feedhandler::OrderBook> v1_books;
    std::unordered_map<uint16_t, v3::OrderBook> v3_books;
    v3::OrderPool pool(kPoolCapacity);

    uint64_t msg_count = 0, compared = 0, mismatches = 0;

    const uint8_t* msg;
    uint16_t len;
    while (reader.next(msg, len)) {
        uint16_t locate = 0;
        bool touched = false;

        switch (msg[0]) {
            case 'A': {
                auto m = itch::AddOrder::parse(msg);
                locate = m.stock_locate;
                v1_books[locate].add_order(m);
                v3_books.try_emplace(locate, pool).first->second.add_order(m);
                touched = true;
                break;
            }
            case 'E': {
                auto m = itch::OrderExecuted::parse(msg);
                locate = m.stock_locate;
                v1_books[locate].execute_order(m);
                v3_books.try_emplace(locate, pool).first->second.execute_order(m);
                touched = true;
                break;
            }
            case 'X': {
                auto m = itch::OrderCancel::parse(msg);
                locate = m.stock_locate;
                v1_books[locate].cancel_order(m);
                v3_books.try_emplace(locate, pool).first->second.cancel_order(m);
                touched = true;
                break;
            }
            case 'D': {
                auto m = itch::OrderDelete::parse(msg);
                locate = m.stock_locate;
                v1_books[locate].delete_order(m);
                v3_books.try_emplace(locate, pool).first->second.delete_order(m);
                touched = true;
                break;
            }
            case 'U': {
                auto m = itch::OrderReplace::parse(msg);
                locate = m.stock_locate;
                v1_books[locate].replace_order(m);
                v3_books.try_emplace(locate, pool).first->second.replace_order(m);
                touched = true;
                break;
            }
            default:
                break;
        }

        ++msg_count;

        if (touched) {
            auto a = v1_books[locate].best_bid_ask();
            auto b = v3_books.at(locate).best_bid_ask();
            ++compared;
            if (a != b) {
                if (mismatches < kMaxMismatchesToPrint) {
                    std::printf("MISMATCH msg#%llu locate=%u type=%c\n",
                                (unsigned long long)msg_count, locate, msg[0]);
                    if (a) std::printf("   v1: bid=%u ask=%u\n", a->first, a->second);
                    else   std::printf("   v1: (none)\n");
                    if (b) std::printf("   v3: bid=%u ask=%u\n", b->first, b->second);
                    else   std::printf("   v3: (none)\n");
                }
                ++mismatches;
            }
        }
    }

    std::printf("\nmessages=%llu  top-of-book comparisons=%llu  mismatches=%llu\n",
                (unsigned long long)msg_count, (unsigned long long)compared,
                (unsigned long long)mismatches);

    return mismatches == 0 ? 0 : 1;
}
