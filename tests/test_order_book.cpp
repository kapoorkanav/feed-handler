#include "order_book.hpp"

#include <cassert>
#include <cstdio>

using namespace feedhandler;
using namespace itch;

int main() {
    OrderBook book;

    // Empty book: no top-of-book.
    assert(!book.best_bid_ask().has_value());

    // Add a bid and an ask -> top-of-book should reflect both.
    book.add_order(AddOrder{0, 0, 0, /*ref*/1, 'B', /*shares*/100, {}, /*price*/10000});
    book.add_order(AddOrder{0, 0, 0, /*ref*/2, 'S', /*shares*/100, {}, /*price*/10100});

    auto top1 = book.best_bid_ask();
    assert(top1.has_value());
    assert(top1->first == 10000);
    assert(top1->second == 10100);

    // A better bid should become the new best bid.
    book.add_order(AddOrder{0, 0, 0, /*ref*/3, 'B', /*shares*/50, {}, /*price*/10050});
    auto top2 = book.best_bid_ask();
    assert(top2->first == 10050);

    // Partial execute on ref 3 (50 shares) should fully drain it, removing it
    // from the book, so best bid falls back to ref 1's price.
    book.execute_order(OrderExecuted{0, 0, 0, /*ref*/3, /*executed*/50, /*match*/0});
    auto top3 = book.best_bid_ask();
    assert(top3->first == 10000);

    // Delete the remaining bid (ref 1) -> no bids left, book has no top-of-book.
    book.delete_order(OrderDelete{0, 0, 0, /*ref*/1});
    assert(!book.best_bid_ask().has_value());

    // Replace the ask (ref 2) with a new price/size under a new ref.
    book.replace_order(OrderReplace{0, 0, 0, /*orig*/2, /*new*/4, /*shares*/75, /*price*/10200});
    auto top4 = book.best_bid_ask();
    assert(!top4.has_value()); // still no bids
    book.add_order(AddOrder{0, 0, 0, /*ref*/5, 'B', /*shares*/10, {}, /*price*/9900});
    auto top5 = book.best_bid_ask();
    assert(top5->first == 9900);
    assert(top5->second == 10200); // confirms replace moved the ask to 10200

    std::printf("order book behaves correctly\n");
    return 0;
}