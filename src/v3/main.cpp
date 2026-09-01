#include "common/itch_messages.hpp"
#include "common/itch_reader.hpp"
#include "v3/order_book.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <unordered_map>

namespace {
constexpr size_t kPoolCapacity = 1u << 20;  // 262144 slots; ~76k
constexpr size_t kRefCapacity = 1u << 18;  
}

int main(int argc, char** argv){
    if(argc!=2){
        std::fprintf(stderr, "usage: %s <path-to-itch-file>\n", argv[0]);
        return 1;
    }

    itch::ItchFileReader reader(argv[1]);
    v3::OrderPool pool(kPoolCapacity);
    v3::RefTable refs(kRefCapacity);
    std::unordered_map<uint16_t, v3::OrderBook> books;

    uint64_t msg_count=0;
    const uint8_t* msg;
    uint16_t len;

    auto start=std::chrono::steady_clock::now();

    while(reader.next(msg, len)){
        switch(msg[0]){
            case 'A': {
                auto m=itch::AddOrder::parse(msg);
                books.try_emplace(m.stock_locate, pool, refs).first->second.add_order(m);
                break;
            }
            case 'E': {
                auto m=itch::OrderExecuted::parse(msg);
                books.try_emplace(m.stock_locate, pool, refs).first->second.execute_order(m);
                break;
            }
            case 'X': {
                auto m=itch::OrderCancel::parse(msg);
                books.try_emplace(m.stock_locate, pool, refs).first->second.cancel_order(m);
                break;
            }
            case 'D': {
                auto m=itch::OrderDelete::parse(msg);
                books.try_emplace(m.stock_locate, pool, refs).first->second.delete_order(m);
                break;
            }
            case 'U': {
                auto m=itch::OrderReplace::parse(msg);
                books.try_emplace(m.stock_locate, pool, refs).first->second.replace_order(m);
                break;
            }
            default: break;
        }
        msg_count++;
        if(msg_count%500000==0){
            std::printf("msgs=%llu  symbols_tracked=%zu\n",
                        (unsigned long long)msg_count, books.size());
        }
    }

    auto end=std::chrono::steady_clock::now();
    double seconds=std::chrono::duration<double>(end - start).count();

    std::printf("\ndone: %llu messages in %.3f s (%.0f msgs/sec)\n",
                (unsigned long long)msg_count, seconds, msg_count/seconds);

    return 0;
}
