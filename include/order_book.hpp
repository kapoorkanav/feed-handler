#pragma once

#include "itch_messages.hpp"

#include <cstdint>
#include <list>
#include <map>
#include <unordered_map>
#include <optional>

namespace feedhandler{
    struct RestingOrder{
        uint64_t ref;
        char side;
        uint32_t price;
        uint32_t shares;
    };

    class OrderBook{
        public:
            void add_order(const itch::AddOrder& m);
            void execute_order(const itch::OrderExecuted& m);
            void cancel_order(const itch::OrderCancel& m);
            void delete_order(const itch::OrderDelete& m);
            void replace_order(const itch::OrderReplace& m);

            std::optional<std::pair<uint32_t, uint32_t>> best_bid_ask() const;

        private:
            struct OrderLocator{
                char side;
                uint32_t price;
                std::list<RestingOrder>::iterator it;
            };

            void remove_locator(const OrderLocator& loc);

            std::map<uint32_t, std::list<RestingOrder>> bids_;
            std::map<uint32_t, std::list<RestingOrder>> asks_;
            std::unordered_map<uint64_t, OrderLocator> ref_index_;
    };
}