#pragma once

#include <cstdint>
#include <vector>

namespace v3{
    constexpr uint32_t kNullIdx=0xFFFFFFFFu;

    struct PooledOrder{
        uint32_t next_idx;
        uint32_t prev_idx;
        uint32_t qty;
        uint32_t level_idx;
    };
    static_assert(sizeof(PooledOrder)==16, "Pooled order must stay 16 bytes");

    class OrderPool{
        public:
            explicit OrderPool(size_t capacity): slots_(capacity), free_head_(0){
                for(size_t i=0; i+1<capacity;i++){
                    slots_[i].next_idx=static_cast<uint32_t>(i+1);
                }
                slots_[capacity-1].next_idx=kNullIdx;
            }

            uint32_t allocate(){
                if(free_head_==kNullIdx) return kNullIdx;
                uint32_t idx=free_head_;
                free_head_=slots_[idx].next_idx;
                return idx;
            }

            void release(uint32_t idx){
                slots_[idx].next_idx=free_head_;
                free_head_=idx;
            }
            
            PooledOrder& operator[](uint32_t idx){
                return slots_[idx];
            }

            const PooledOrder& operator [](uint32_t idx) const{
                return slots_[idx];
            }

        private:
            std::vector<PooledOrder> slots_;
            uint32_t free_head_;
    };
}