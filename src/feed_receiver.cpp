#include "itch_messages.hpp"
#include "mold_udp64.hpp"
#include "order_book.hpp"

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cerrno>
#include <unordered_map>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

constexpr const char* kMulticastAddr="239.1.1.1";
constexpr uint16_t kPort=30000;
constexpr size_t kRecvBufSize=2048;


int main(){
    int sock=socket(AF_INET, SOCK_DGRAM, 0);
    if(sock<0){
        std::perror("socket");
        return 1;
    }
    struct sockaddr_in addr{};
    addr.sin_family=AF_INET;
    addr.sin_addr.s_addr=htonl(INADDR_ANY);
    addr.sin_port=htons(kPort);

    int want_rcvbuf = 16 * 1024 * 1024;
    setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &want_rcvbuf, sizeof(want_rcvbuf));

    int got_rcvbuf = 0;
    socklen_t optlen = sizeof(got_rcvbuf);
    getsockopt(sock, SOL_SOCKET, SO_RCVBUF, &got_rcvbuf, &optlen);
    std::printf("requested rcvbuf=%d bytes, kernel actually gave us=%d bytes\n",
                want_rcvbuf, got_rcvbuf);

    int reuse = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        std::perror("SO_REUSEADDR");
        return 1;
    }

    if(bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr))<0){
        std::perror("bind");
        return 1;
    }
    ip_mreq mreq{};
    inet_pton(AF_INET, kMulticastAddr, &mreq.imr_multiaddr);
    mreq.imr_interface.s_addr=htonl(INADDR_ANY);
    if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
        std::perror("IP_ADD_MEMBERSHIP");
        return 1;
    }

    timeval tv{};
    tv.tv_sec=3;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    std::unordered_map<uint16_t, feedhandler::OrderBook> books;

    uint8_t buf[kRecvBufSize];
    uint64_t expected_seq=0;
    uint64_t packets=0, messages=0, gaps=0, lost=0, dups=0;

    std::printf("listening on %s:%u ...\n", kMulticastAddr, kPort);

    while(true){
        ssize_t n=recvfrom(sock, buf, sizeof(buf), 0, nullptr, nullptr);
        if(n<0){
            if(errno==EAGAIN||errno==EWOULDBLOCK) break;
            std::perror("recvfrom");
            break;
        }
        if(static_cast<size_t>(n)<mold::kHeaderSize) continue;

        uint64_t seq=itch::read_be64(buf+10);
        uint16_t count=itch::read_be16(buf+18);
        packets++;

        if(expected_seq==0) expected_seq=seq;
        if(seq>expected_seq){
            gaps++;
            lost+=(seq-expected_seq);
        }
        else if(seq<expected_seq){
            dups++;
            continue;
        }

        size_t off=mold::kHeaderSize;

        for(uint16_t i=0;i<count;i++){
            if(off+2>static_cast<size_t>(n)) break;
            uint16_t len=itch::read_be16(buf+off);
            off+=2;
            if(off+len>static_cast<size_t>(n)) break;

            const uint8_t* msg=buf+off;
            switch(msg[0]){
                case 'A':{
                    auto m=itch::AddOrder::parse(msg);
                    books[m.stock_locate].add_order(m);
                    break;
                }
                case 'E':{
                    auto m=itch::OrderExecuted::parse(msg);
                    books[m.stock_locate].execute_order(m);
                    break;
                }
                case 'X':{
                    auto m=itch::OrderCancel::parse(msg);
                    books[m.stock_locate].cancel_order(m);
                    break;
                }
                case 'D':{
                    auto m=itch::OrderDelete::parse(msg);
                    books[m.stock_locate].delete_order(m);
                    break;
                }
                case 'U':{
                    auto m=itch::OrderReplace::parse(msg);
                    books[m.stock_locate].replace_order(m);
                    break;
                }
                default: break;
            }
            off+=len;
            messages++;
        }
        expected_seq=seq+count;
    }
    std::printf("\npackets=%llu messages=%llu symbols=%zu\n",
                (unsigned long long)packets, (unsigned long long)messages, books.size());
    std::printf("gaps=%llu messages_lost=%llu duplicates=%llu\n",
                (unsigned long long)gaps, (unsigned long long)lost,
                (unsigned long long)dups);

    close(sock);
    return 0;
}