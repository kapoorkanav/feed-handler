#include "v1/order_book.hpp"

namespace feedhandler{
    void OrderBook::remove_locator(const OrderLocator& loc){
        auto& levels=(loc.side=='B')?bids_:asks_;
        auto level_it=levels.find(loc.price);
        level_it->second.erase(loc.it);
        if(level_it->second.empty()){
            levels.erase(level_it);
        }
    }

    void OrderBook::add_order(const itch::AddOrder& m){
        auto& levels=(m.side=='B')?bids_:asks_;
        auto& level=levels[m.price];
        level.push_back(RestingOrder{m.order_ref, m.side, m.price, m.shares});
        ref_index_[m.order_ref]=OrderLocator{m.side, m.price, std::prev(level.end())};
    }

    void OrderBook::execute_order(const itch::OrderExecuted& m){
        auto idx_it=ref_index_.find(m.order_ref);
        if(idx_it==ref_index_.end()) return;

        OrderLocator& loc=idx_it->second;
        loc.it->shares-=m.executed_shares;

        if(loc.it->shares==0){
            remove_locator(loc);
            ref_index_.erase(idx_it);
        }
    }

    void OrderBook::cancel_order(const itch::OrderCancel& m){
        auto idx_it=ref_index_.find(m.order_ref);
        if(idx_it==ref_index_.end()) return;

        OrderLocator& loc=idx_it->second;
        loc.it->shares-=m.canceled_shares;

        if(loc.it->shares==0){
            remove_locator(loc);
            ref_index_.erase(idx_it);
        }
    }

    void OrderBook::delete_order(const itch::OrderDelete& m){
        auto idx_it=ref_index_.find(m.order_ref);
        if(idx_it==ref_index_.end()) return;

        remove_locator(idx_it->second);
        ref_index_.erase(idx_it);
    }

    void OrderBook::replace_order(const itch::OrderReplace& m){
        auto idx_it=ref_index_.find(m.original_order_ref);
        if(idx_it==ref_index_.end()) return;

        char side=idx_it->second.side;

        remove_locator(idx_it->second);
        ref_index_.erase(idx_it);

        auto &levels=(side=='B')?bids_:asks_;
        auto &level=levels[m.price];
        level.push_back(RestingOrder{m.new_order_ref, side, m.price, m.shares});
        ref_index_[m.new_order_ref]=OrderLocator{side, m.price, std::prev(level.end())};
    }

    std::optional<std::pair<uint32_t, uint32_t>> OrderBook::best_bid_ask() const{
        if(bids_.empty()||asks_.empty()) return std::nullopt;
        return std::make_pair(bids_.rbegin()->first, asks_.begin()->first);
    }
}