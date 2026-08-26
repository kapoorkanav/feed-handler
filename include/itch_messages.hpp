#pragma once
#include <cstdint>
#include <cstring>

namespace itch{
    inline uint16_t read_be16(const uint8_t* p){
        uint16_t v;
        std::memcpy(&v, p, sizeof(v));
        return __builtin_bswap16(v);
    }

    inline uint32_t read_be32(const uint8_t* p){
        uint32_t v;
        std::memcpy(&v, p, sizeof(v));
        return __builtin_bswap32(v);
    }

    inline uint64_t read_be48(const uint8_t* p){
        uint64_t v=0;
        for(int i=0;i<6;i++){
            v=(v<<8)|p[i];
        }
        return v;
    }

    inline uint64_t read_be64(const uint8_t* p){
        uint64_t v;
        std::memcpy(&v, p, sizeof(v));
        return __builtin_bswap64(v);
    }

    enum class MsgType: uint8_t{
        AddOrder='A',
        OrderExecuted='E',
        OrderCancel='X',
        OrderDelete='D',
        OrderReplace='U',
    };

    struct AddOrder{
        static constexpr size_t kWireSize=36;

        uint16_t stock_locate;
        uint16_t tracking_number;
        uint64_t timestamp_ns;
        uint64_t order_ref;
        char side;
        uint32_t shares;
        char stock[8];
        uint32_t price;

        static AddOrder parse(const uint8_t* wire){
            AddOrder m;
            m.stock_locate=read_be16(wire+1);
            m.tracking_number=read_be16(wire+3);
            m.timestamp_ns=read_be48(wire+5);
            m.order_ref=read_be64(wire+11);
            m.side=static_cast<char>(wire[19]);
            m.shares=read_be32(wire+20);
            std::memcpy(m.stock, wire+24, 8);
            m.price=read_be32(wire+32);
            return m;
        }

    };

    struct OrderExecuted{
        static constexpr size_t kWireSize=31;
        
        uint16_t stock_locate;
        uint16_t tracking_number;
        uint64_t timestamp_ns;
        uint64_t order_ref;
        uint32_t executed_shares;
        uint64_t match_number;

        static OrderExecuted parse(const uint8_t* wire) {
            OrderExecuted m;
            m.stock_locate=read_be16(wire + 1);
            m.tracking_number=read_be16(wire + 3);
            m.timestamp_ns=read_be48(wire + 5);
            m.order_ref=read_be64(wire + 11);
            m.executed_shares=read_be32(wire + 19);
            m.match_number=read_be64(wire + 23);
            return m;
        }
    };

    struct OrderCancel {
        static constexpr size_t kWireSize = 23;

        uint16_t stock_locate;
        uint16_t tracking_number;
        uint64_t timestamp_ns;
        uint64_t order_ref;
        uint32_t canceled_shares;

        static OrderCancel parse(const uint8_t* wire) {
            OrderCancel m;
            m.stock_locate=read_be16(wire + 1);
            m.tracking_number=read_be16(wire + 3);
            m.timestamp_ns=read_be48(wire + 5);
            m.order_ref=read_be64(wire + 11);
            m.canceled_shares=read_be32(wire + 19);
            return m;
        }
    };

    struct OrderDelete {
        static constexpr size_t kWireSize = 19;

        uint16_t stock_locate;
        uint16_t tracking_number;
        uint64_t timestamp_ns;
        uint64_t order_ref;

        static OrderDelete parse(const uint8_t* wire) {
            OrderDelete m;
            m.stock_locate=read_be16(wire + 1);
            m.tracking_number=read_be16(wire + 3);
            m.timestamp_ns=read_be48(wire + 5);
            m.order_ref=read_be64(wire + 11);
            return m;
        }
    };

    struct OrderReplace {
        static constexpr size_t kWireSize = 35;

        uint16_t stock_locate;
        uint16_t tracking_number;
        uint64_t timestamp_ns;
        uint64_t original_order_ref;
        uint64_t new_order_ref;
        uint32_t shares;
        uint32_t price;

        static OrderReplace parse(const uint8_t* wire) {
            OrderReplace m;
            m.stock_locate=read_be16(wire + 1);
            m.tracking_number=read_be16(wire + 3);
            m.timestamp_ns=read_be48(wire + 5);
            m.original_order_ref=read_be64(wire + 11);
            m.new_order_ref=read_be64(wire + 19);
            m.shares= read_be32(wire + 27);
            m.price=read_be32(wire + 31);
            return m;
        }
    };


}