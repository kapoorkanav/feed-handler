#pragma once

#include "common/itch_messages.hpp"
#include "v3/object_pool.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace v3{
    struct PriceLevel{
        uint32_t head_idx;
        uint32_t tail_idx;
        uint32_t total_qty;
        uint32_t order_count;
    };
    static_assert(sizeof(PriceLevel)==16, "PriceLevel must stay 16");

    class OrderBook{
        public:
            static constexpr uint32_t kWindow=1024;
            static constexpr uint32_t kAskBase=kWindow;

            explicit OrderBook(OrderPool& pool);

            void add_order(const itch::AddOrder& m);
            void execute_order(const itch::OrderExecuted& m);
            void cancel_order(const itch::OrderCancel& m);
            void delete_order(const itch::OrderDelete& m);
            void replace_order(const itch::OrderReplace& m);

            std::optional<std::pair<uint32_t, uint32_t>> best_bid_ask() const;

        private:
            uint32_t level_for(uint32_t price, char side);
            char side_of(uint32_t level_idx) const;
            uint32_t price_of(uint32_t level_idx) const;

            void link_order(uint32_t level_idx, uint32_t order_idx, uint32_t qty);
            void unlink_order(uint32_t order_idx);
            void rescan_best(char side);

            OrderPool& pool_;

            int64_t base_cents_=-1;
            bool base_set_=false;
            std::vector<PriceLevel> levels_;
            std::map<uint32_t, uint32_t> overflow_bid_;
            std::map<uint32_t, uint32_t> overflow_ask_;
            std::vector<char> overflow_side_;

            int32_t best_bid_idx_=-1;
            int32_t best_ask_idx_=-1;

            std::unordered_map<uint64_t, uint32_t> ref_index_;
    };
}