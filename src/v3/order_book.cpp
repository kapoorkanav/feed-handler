#include "v3/order_book.hpp"

namespace v3{
    // OrderBook::OrderBook(OrderPool& pool, RefTable& refs): pool_(pool), refs_(refs), levels_(2*kWindow, PriceLevel{kNullIdx, kNullIdx, 0,0}){}
    OrderBook::OrderBook(OrderPool& pool, RefTable& refs)
        : pool_(pool), refs_(refs),
          levels_(2*kWindow, PriceLevel{kNullIdx, kNullIdx, 0, 0}) {
        levels_.reserve(2*kWindow + 512);
    }

    char OrderBook::side_of(uint32_t level_idx) const{
        if(level_idx<kAskBase) return 'B';
        if(level_idx<2*kWindow) return 'S';
        return overflow_side_[level_idx-(2*kWindow)];
    }

    uint32_t OrderBook::price_of(uint32_t level_idx) const{
        if(level_idx<kAskBase){
            return static_cast<uint32_t>((base_cents_+level_idx)*100);
        }
        if(level_idx<2*kWindow){
            return static_cast<uint32_t>((base_cents_+(level_idx-kAskBase))*100);
        }
        const auto& ovf=(side_of(level_idx)=='B')?overflow_bid_:overflow_ask_;
        for(const auto& kv:ovf){
            if(kv.second==level_idx) return kv.first;
        }
        return 0;
    }

    uint32_t OrderBook::level_for(uint32_t price, char side){
        if(!base_set_){
            base_cents_=static_cast<int64_t>(price/100)-kWindow/2;
            base_set_=true;
        }
        if(price%100==0){
            int64_t off=static_cast<int64_t>(price/100)-base_cents_;
            if(off>=0&&off<static_cast<int64_t>(kWindow)){
                return (side == 'B')?static_cast<uint32_t>(off):kAskBase+static_cast<uint32_t>(off);
            }
        }
        auto& ovf=(side=='B')?overflow_bid_:overflow_ask_;
        auto it=ovf.find(price);
        if(it!=ovf.end()) return it->second;

        uint32_t idx=static_cast<uint32_t>(levels_.size());
        levels_.push_back(PriceLevel{kNullIdx, kNullIdx, 0, 0});
        overflow_side_.push_back(side);
        ovf[price]=idx;
        return idx;
    }

    void OrderBook::link_order(uint32_t level_idx, uint32_t order_idx, uint32_t qty){
        PriceLevel& lvl=levels_[level_idx];
        PooledOrder& o=pool_[order_idx];

        o.next_idx=kNullIdx;
        o.prev_idx=lvl.tail_idx;
        o.level_idx=level_idx;
        o.qty=qty;

        if(lvl.tail_idx!=kNullIdx) pool_[lvl.tail_idx].next_idx=order_idx;
        else lvl.head_idx=order_idx;

        lvl.tail_idx=order_idx;
        lvl.order_count++;
        lvl.total_qty+=qty;

        if(level_idx<2*kWindow){
            occupied_[level_idx>>6] |= (1ull<<(level_idx&63));
        }

        if(level_idx<kAskBase){
            if(static_cast<int32_t>(level_idx)>best_bid_idx_){
                best_bid_idx_=static_cast<int32_t>(level_idx);
            }
        }
        else if(level_idx<2*kWindow){
            if(best_ask_idx_<0||static_cast<int32_t>(level_idx)<best_ask_idx_){
                best_ask_idx_=static_cast<int32_t>(level_idx);
            }
        }
    }

    void OrderBook::unlink_order(uint32_t order_idx){
        PooledOrder& o=pool_[order_idx];
        uint32_t level_idx=o.level_idx;
        PriceLevel& lvl=levels_[level_idx];

        if(o.prev_idx!=kNullIdx) pool_[o.prev_idx].next_idx=o.next_idx;
        else lvl.head_idx=o.next_idx;

        if(o.next_idx!=kNullIdx) pool_[o.next_idx].prev_idx=o.prev_idx;
        else lvl.tail_idx=o.prev_idx;

        lvl.total_qty-=o.qty;
        lvl.order_count--;

        if(lvl.head_idx==kNullIdx){
            if(level_idx<2*kWindow){
                occupied_[level_idx>>6] &= ~(1ull<<(level_idx&63));
            }
            if(static_cast<int32_t>(level_idx)==best_bid_idx_) rescan_best('B');
            if(static_cast<int32_t>(level_idx)==best_ask_idx_) rescan_best('S');
        }
    }

    int32_t OrderBook::find_prev_set(int32_t from) const{
        if(from<0) return -1;
        int32_t w=from>>6;
        int32_t b=from&63;
        uint64_t word=occupied_[w]&((b==63)?~0ull:((1ull<<(b+1))-1));
        while(true){
            if(word) return (w<<6)+(63-__builtin_clzll(word));
            if(w==0) return -1;
            --w;
            word=occupied_[w];
        }
    }

    int32_t OrderBook::find_next_set(int32_t from) const {
        constexpr int32_t limit=2*kWindow;
        if(from>=limit) return -1;
        if(from<0) from=0;
        int32_t w=from>>6;
        int32_t b=from&63;
        uint64_t word=occupied_[w]&(~0ull<<b);
        constexpr int32_t last_word=(limit-1)>>6;
        while(true){
            if(word) return (w<<6)+__builtin_ctzll(word);
            if(w>=last_word) return -1;
            ++w;
            word=occupied_[w];
        }
    }

    void OrderBook::rescan_best(char side){
        if(side=='B'){
            best_bid_idx_=find_prev_set(best_bid_idx_);
        }
        else{
            best_ask_idx_=find_next_set(best_ask_idx_);
        }
    }

    void OrderBook::add_order(const itch::AddOrder &m){
        uint32_t idx=pool_.allocate();
        if(idx==kNullIdx) return;
        uint32_t li=level_for(m.price, m.side);
        link_order(li, idx, m.shares);
        refs_.insert(m.order_ref, idx);
    }

    void OrderBook::execute_order(const itch::OrderExecuted& m){
        uint32_t idx=refs_.find(m.order_ref);
        if(idx==kNullIdx) return;
        PooledOrder& o=pool_[idx];
        if(m.executed_shares>=o.qty){
            unlink_order(idx);
            pool_.release(idx);
            refs_.erase(m.order_ref);
        }
        else{
            levels_[o.level_idx].total_qty-=m.executed_shares;
            o.qty-=m.executed_shares;
        }
    }

    void OrderBook::cancel_order(const itch::OrderCancel& m){
        uint32_t idx=refs_.find(m.order_ref);
        if(idx==kNullIdx) return;
        PooledOrder &o=pool_[idx];
        if(m.canceled_shares>=o.qty){
            unlink_order(idx);
            pool_.release(idx);
            refs_.erase(m.order_ref);
        }
        else{
            levels_[o.level_idx].total_qty-=m.canceled_shares;
            o.qty-=m.canceled_shares;
        }
    }

    void OrderBook::delete_order(const itch::OrderDelete& m){
        uint32_t idx=refs_.find(m.order_ref);
        if(idx==kNullIdx) return;
        unlink_order(idx);
        pool_.release(idx);
        refs_.erase(m.order_ref);
    }

    void OrderBook::replace_order(const itch::OrderReplace& m){
        uint32_t old_idx=refs_.find(m.original_order_ref);
        if(old_idx==kNullIdx) return;
        char side=side_of(pool_[old_idx].level_idx);

        unlink_order(old_idx);
        pool_.release(old_idx);
        refs_.erase(m.original_order_ref);

        uint32_t idx=pool_.allocate();
        if(idx==kNullIdx) return;
        uint32_t li=level_for(m.price, side);
        link_order(li, idx, m.shares);
        refs_.insert(m.new_order_ref, idx);
    }

    std::optional<std::pair<uint32_t, uint32_t>> OrderBook::best_bid_ask() const {
        bool have_bid=false;
        uint32_t bid=0;
        if(best_bid_idx_>=0){
            bid=price_of(static_cast<uint32_t>(best_bid_idx_));
            have_bid=true;
        }
        for(auto it=overflow_bid_.rbegin();it!=overflow_bid_.rend();++it){
            if(levels_[it->second].head_idx!=kNullIdx){
                if(!have_bid||it->first>bid) bid=it->first;
                have_bid=true;
                break;
            }
        }

        bool have_ask=false;
        uint32_t ask=0;
        if(best_ask_idx_>=0){
            ask=price_of(static_cast<uint32_t>(best_ask_idx_));
            have_ask=true;
        }
        for(auto it=overflow_ask_.begin();it!=overflow_ask_.end();++it){
            if(levels_[it->second].head_idx!=kNullIdx){
                if(!have_ask||it->first<ask) ask=it->first;
                have_ask=true;
                break;
            }
        }

        if(!have_bid||!have_ask) return std::nullopt;
        return std::make_pair(bid, ask);
    }
}