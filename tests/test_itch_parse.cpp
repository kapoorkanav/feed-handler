#include "itch_messages.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>

int main() {
    uint8_t wire[36] = {
        0x41,                                           // type 'A'
        0x00, 0x2A,                                     // stock_locate = 42
        0x00, 0x07,                                     // tracking_number = 7
        0x00, 0x00, 0x07, 0x5B, 0xCD, 0x15,              // timestamp_ns = 123456789
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x64,  // order_ref = 100
        0x42,                                           // side = 'B'
        0x00, 0x00, 0x01, 0xF4,                          // shares = 500
        0x41, 0x41, 0x50, 0x4C, 0x20, 0x20, 0x20, 0x20,  // stock = "AAPL    "
        0x00, 0x16, 0xED, 0x24,                          // price = 1502500
    };

    itch::AddOrder m = itch::AddOrder::parse(wire);

    assert(m.stock_locate == 42);
    assert(m.tracking_number == 7);
    assert(m.timestamp_ns == 123456789);
    assert(m.order_ref == 100);
    assert(m.side == 'B');
    assert(m.shares == 500);
    assert(std::memcmp(m.stock, "AAPL    ", 8) == 0);
    assert(m.price == 1502500);

    std::printf("all fields parsed correctly\n");
    return 0;
}
