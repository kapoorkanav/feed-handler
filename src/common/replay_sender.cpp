#include "common/itch_reader.hpp"
#include "common/mold_udp64.hpp"

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <vector>
#include <chrono>
#include <thread>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

constexpr const char* kMulticastAddr="239.1.1.1";
constexpr uint16_t kPort=30000;
const size_t kMaxPacketSize=1400;
constexpr double kTargetMsgsPerSec=1'000'000.0;

int main(int argc, char** argv){
    if(argc!=2){
        std::fprintf(stderr, "usage %s <path-to-itch-file>\n", argv[0]);
        return 1;
    }

    int sock=socket(AF_INET, SOCK_DGRAM, 0);
    if(sock<0){
        std::perror("socket");
        return 1;
    }

    unsigned char ttl=1;
    setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));

    struct sockaddr_in dest{};
    dest.sin_family=AF_INET;
    dest.sin_port=htons(kPort);
    inet_pton(AF_INET, kMulticastAddr, &dest.sin_addr);

    itch::ItchFileReader reader(argv[1]);

    uint8_t packet[kMaxPacketSize];
    size_t packet_used=mold::kHeaderSize;
    uint64_t packet_first_seq=1;
    uint16_t packet_msg_count=0;

    uint64_t next_seq=1;
    uint64_t total_sent=0;

    auto pace_start = std::chrono::steady_clock::now();

    auto flush_packet=[&](){
        if(packet_msg_count==0) return;

        std::memset(packet, ' ', mold::kSessionSize);
        std::memcpy(packet, "SESSION001", mold::kSessionSize);
        mold::write_be64(packet+10, packet_first_seq);
        mold::write_be16(packet+18, packet_msg_count);

        sendto(sock, packet, packet_used, 0, reinterpret_cast<sockaddr*>(&dest), sizeof(dest));

        total_sent+=packet_msg_count;
        double expected_elapsed = static_cast<double>(total_sent) / kTargetMsgsPerSec;
        double actual_elapsed = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - pace_start).count();
        if (actual_elapsed < expected_elapsed) {
            std::this_thread::sleep_for(
                std::chrono::duration<double>(expected_elapsed - actual_elapsed));
        }
        packet_used=mold::kHeaderSize;
        packet_msg_count=0;
    };

    const uint8_t* msg;
    uint16_t len;
    while(reader.next(msg, len)){
        size_t needed=2+len;
        
        if(packet_used+needed>kMaxPacketSize){
            flush_packet();
        }

        if(packet_msg_count==0){
            packet_first_seq=next_seq;
        }

        mold::write_be16(packet+packet_used, len);
        std::memcpy(packet+packet_used+2, msg, len);
        packet_used+=needed;
        packet_msg_count++;
        next_seq++;
    }
    flush_packet();

    std::printf("sent %llu messages\n", (unsigned long long)total_sent);
    close(sock);
    return 0;
}