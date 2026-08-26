#pragma once

#include "itch_messages.hpp"

#include <cstdint>
#include <string>
#include <stdexcept>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace itch{
    class ItchFileReader{
        public:
            explicit ItchFileReader(const std::string& path);
            ~ItchFileReader();

            ItchFileReader(const ItchFileReader&)=delete;
            ItchFileReader& operator=(const ItchFileReader&)=delete;

            bool next(const uint8_t*& msg, uint16_t& len);

        private:
            const uint8_t* base_=nullptr;
            size_t size_=0;
            size_t offset_=0;
    };
}