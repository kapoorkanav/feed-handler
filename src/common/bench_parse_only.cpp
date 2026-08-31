#include "common/itch_messages.hpp"
#include "common/itch_reader.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>

int main(int argc, char** argv){
    if(argc!=2){
        std::fprintf(stderr, "usage: %s <path-to-itch-file>\n", argv[0]);
        return 1;
    }

    itch::ItchFileReader reader(argv[1]);

    uint64_t msg_count=0;
    uint64_t sink=0;          // stops the compiler eliding the parses entirely
    const uint8_t* msg;
    uint16_t len;

    auto start=std::chrono::steady_clock::now();

    while(reader.next(msg, len)){
        switch(msg[0]){
            case 'A': {
                auto m=itch::AddOrder::parse(msg);
                sink+=m.order_ref+m.price+m.shares+m.stock_locate;
                break;
            }
            case 'E': {
                auto m=itch::OrderExecuted::parse(msg);
                sink+=m.order_ref+m.executed_shares+m.stock_locate;
                break;
            }
            case 'X': {
                auto m=itch::OrderCancel::parse(msg);
                sink+=m.order_ref+m.canceled_shares+m.stock_locate;
                break;
            }
            case 'D': {
                auto m=itch::OrderDelete::parse(msg);
                sink+=m.order_ref+m.stock_locate;
                break;
            }
            case 'U': {
                auto m=itch::OrderReplace::parse(msg);
                sink+=m.original_order_ref+m.new_order_ref+m.price+m.shares;
                break;
            }
            default: break;
        }
        msg_count++;
    }

    auto end=std::chrono::steady_clock::now();
    double seconds=std::chrono::duration<double>(end-start).count();

    std::printf("parse-only: %llu messages in %.3f s (%.0f msgs/sec)  [sink=%llu]\n",
                (unsigned long long)msg_count, seconds, msg_count/seconds,
                (unsigned long long)sink);
    return 0;
}
