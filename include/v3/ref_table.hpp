#pragma once
#include "v3/object_pool.hpp"

#include <cstdint>
#include <vector>

namespace v3{
    class RefTable{
        public:
            static constexpr uint64_t kEmpty=~0ull;
            explicit RefTable(size_t capacity_pow2): slots_(capacity_pow2, Slot{kEmpty, 0, 0}), mask_(capacity_pow2-1){}

            void insert(uint64_t key, uint32_t value){
                if(size_*2>=slots_.size()) return;
                size_t i=probe(key);
                if(slots_[i].key==kEmpty){
                    slots_[i].key=key;
                    size_++;
                }
                slots_[i].value=value;
            }

            uint32_t find(uint64_t key) const{
                size_t i=probe(key);
                return slots_[i].key==key?slots_[i].value:kNullIdx;
            }

            void erase(uint64_t key){
                size_t i=probe(key);
                if(slots_[i].key!=key) return;

                size_t j=i;
                while(true){
                    j=(j+1)&mask_;
                    if(slots_[j].key==kEmpty) break;
                    size_t k=hash(slots_[j].key)&mask_;
                    bool can_move=(j>i)?(k<=i||k>j):(k<=i&&k>j);
                    if(can_move){
                        slots_[i]=slots_[j];
                        i=j;
                    }
                }
                slots_[i].key=kEmpty;
                size_--;
            }
            size_t size() const{
                return size_;
            }
            size_t capacity() const{
                return slots_.size();
            }
        private:
            struct Slot{
                uint64_t key;
                uint32_t value;
                uint32_t pad;
            };
            static_assert(sizeof(Slot)==16, "Slot must stay 16 bytes");

            static uint64_t hash(uint64_t x) {
                x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ull;
                x ^= x >> 27; x *= 0x94d049bb133111ebull;
                x ^= x >> 31;
                return x;
            }

            size_t probe(uint64_t key) const{
                size_t i=hash(key)&mask_;
                while(slots_[i].key!=kEmpty&&slots_[i].key!=key){
                    i=(i+1)&mask_;
                }
                return i;
            }

            std::vector<Slot> slots_;
            size_t mask_;
            size_t size_=0;
    };
}