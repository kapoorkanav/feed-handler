#include "common/itch_messages.hpp"
#include "common/mold_udp64.hpp"
#include "v1/order_book.hpp"

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <vector>
#include <map>
#include <unordered_map>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <liburing.h>

constexpr const char* kMulticastAddr="239.1.1.1";
constexpr uint16_t kPort=30000;
constexpr unsigned kQueueDepth=32;
constexpr size_t kBufSize=2048;

int main(){
    int sock=socket(AF_INET, SOCK_DGRAM, 0);
    if(sock<0){
        perror("socket");
        return 1;
    }
    
    int reuse=1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family=AF_INET;
    addr.sin_addr.s_addr=htonl(INADDR_ANY);
    addr.sin_port=htons(kPort);
    if(bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr))<0){
        perror("bind");
        return 1;
    }

    ip_mreq mreq{};
    inet_pton(AF_INET, kMulticastAddr, &mreq.imr_multiaddr);
    mreq.imr_interface.s_addr=htonl(INADDR_ANY);
    if(setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq))<0){
        perror("IP_ADD_MEMBERSHIP");
        return 1;
    }

    struct io_uring ring;
    int ret=io_uring_queue_init(kQueueDepth, &ring, 0);
    if(ret<0){
        std::fprintf(stderr, "io_uring_queue_init failed: %d\n", ret);
        return 1;
    }

    std::vector<std::vector<uint8_t>> bufs(kQueueDepth, std::vector<uint8_t> (kBufSize));

    auto prep_recv=[&](unsigned idx){
        struct io_uring_sqe* sqe=io_uring_get_sqe(&ring);
        io_uring_prep_recv(sqe, sock, bufs[idx].data(), kBufSize, 0);
        io_uring_sqe_set_data(sqe, reinterpret_cast<void*>(static_cast<uintptr_t>(idx)));
    };

    for(unsigned i=0;i<kQueueDepth;i++){
        prep_recv(i);
    }
    io_uring_submit(&ring);

    std::unordered_map<uint16_t, feedhandler::OrderBook> books;
    uint64_t high_seq=0;
    std::map<uint64_t, uint16_t> pending_gaps;
    uint64_t packets=0, messages=0, gaps=0, dups=0, truly_lost=0, recovered=0;

    std::printf("listening on %s:%u via io_uring (queue depth=%u)...\n", kMulticastAddr, kPort, kQueueDepth);

    __kernel_timespec ts{};
    ts.tv_sec=3;

    while(true){
        struct io_uring_cqe* first_cqe;
        int wait_ret=io_uring_wait_cqe_timeout(&ring, &first_cqe, &ts);
        if(wait_ret==-ETIME) break;
        if(wait_ret<0){ 
            std::fprintf(stderr, "wait_cqe failed: %d\n", wait_ret);
            break;
        }

        struct io_uring_cqe* cqe;
        unsigned head;
        unsigned completed=0;

        io_uring_for_each_cqe(&ring, head, cqe){
            unsigned idx=static_cast<unsigned>(reinterpret_cast<uintptr_t>(io_uring_cqe_get_data(cqe)));
            int n=cqe->res;

            if(n>0&&static_cast<size_t>(n)>=mold::kHeaderSize){
                const uint8_t* buf=bufs[idx].data();
                uint64_t seq=itch::read_be64(buf+10);
                uint16_t count=itch::read_be16(buf+18);
                packets++;

                if(high_seq==0) high_seq=seq;

                bool accept=true;

                if(seq==high_seq){
                    high_seq+=count;
                }
                else if(seq>high_seq){
                    gaps++;
                    pending_gaps[high_seq]=static_cast<uint16_t>(seq-high_seq);
                    high_seq=seq+count;
                }
                else{
                    auto it=pending_gaps.upper_bound(seq);
                    bool recovered_here=false;
                    if(it!=pending_gaps.begin()){
                        it--;
                        uint64_t gap_start=it->first;
                        uint64_t gap_len=it->second;
                        if(seq>=gap_start&&seq+count<=gap_start+gap_len){
                            recovered_here=true;
                            recovered+=count;
                            if(seq==gap_start&&count==gap_len){
                                pending_gaps.erase(it);
                            }
                            else if(seq==gap_start){
                                pending_gaps.erase(it);
                                pending_gaps[seq+count]=static_cast<uint16_t>(gap_len-count);
                            }
                            else if(seq+count==gap_start+gap_len){
                                it->second=static_cast<uint16_t>(seq-gap_start);
                            }
                            else{
                                pending_gaps.erase(it);
                                pending_gaps[gap_start]=static_cast<uint16_t>(seq-gap_start);
                                pending_gaps[seq+count]=static_cast<uint16_t>((gap_start+gap_len)-(seq+count));
                            }
                        }
                    }
                    if(!recovered_here){
                        dups++;
                        accept=false;
                    }
                }

                if(accept){
                    size_t off=mold::kHeaderSize;
                    for(uint16_t i=0;i<count;i++){
                        if(off+2>static_cast<size_t>(n)) break;
                        uint16_t len=itch::read_be16(buf+off);
                        off+=2;
                        if(off+len>static_cast<size_t>(n)) break;

                        const uint8_t* msg=buf+off;
                        switch(msg[0]){
                            case 'A':{
                                auto m = itch::AddOrder::parse(msg);
                                books[m.stock_locate].add_order(m); 
                                break;
                            }
                            case 'E':{
                                auto m = itch::OrderExecuted::parse(msg);
                                books[m.stock_locate].execute_order(m); 
                                break; 
                            }
                            case 'X':{
                                auto m = itch::OrderCancel::parse(msg);
                                books[m.stock_locate].cancel_order(m); 
                                break;
                            }
                            case 'D':{
                                auto m = itch::OrderDelete::parse(msg);
                                books[m.stock_locate].delete_order(m);
                                break; 
                            }
                            case 'U':{
                                auto m = itch::OrderReplace::parse(msg);
                                books[m.stock_locate].replace_order(m); 
                                break;
                            }
                            default: break;
                        }
                        off+=len;
                        messages++;
                    }
                }
            }
            prep_recv(idx);
            completed++;
        }
        io_uring_cq_advance(&ring, completed);
        if(completed>0) io_uring_submit(&ring);
    }
    for (auto& kv : pending_gaps) truly_lost += kv.second;

    std::printf("\npackets=%llu messages=%llu symbols=%zu\n",
                (unsigned long long)packets, (unsigned long long)messages, books.size());
    std::printf("gap_events=%llu recovered=%llu truly_lost=%llu duplicates=%llu\n",
                (unsigned long long)gaps, (unsigned long long)recovered,
                (unsigned long long)truly_lost, (unsigned long long)dups);

    io_uring_queue_exit(&ring);
    close(sock);
    return 0;
}