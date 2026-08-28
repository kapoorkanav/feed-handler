#pragma once

#include <cstdint>
#include <cstring>

namespace mold{
    constexpr size_t kSessionSize=10;
    constexpr size_t kHeaderSize=20;
    
    inline void write_be16(uint8_t* p, uint16_t v){
        uint16_t be=__builtin_bswap16(v);
        std::memcpy(p, &be, 2);
    }

    inline void write_be64(uint8_t* p, uint64_t v){
        uint64_t be=__builtin_bswap64(v);
        std::memcpy(p, &be, 8);
    }
}