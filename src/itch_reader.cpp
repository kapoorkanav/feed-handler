#include "itch_reader.hpp"

namespace itch{
    ItchFileReader::ItchFileReader(const std::string& path){
        int fd=open(path.c_str(), O_RDONLY);
        if(fd<0){
            throw std::runtime_error("failed to open ITCH data file: "+path);

        }

        struct stat st;
        if(fstat(fd, &st)<0){
            close(fd);
            throw std::runtime_error("failed to stat ITCH data file:  "+path);
        }
        size_=static_cast<size_t>(st.st_size);

        void* mapped=mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd, 0);
        close(fd);

        if(mapped==MAP_FAILED){
            throw std::runtime_error("failed to mmap ITCH data file: "+path);
        }
        base_=static_cast<const uint8_t*>(mapped);
    }

    ItchFileReader::~ItchFileReader(){
        if(base_!=nullptr){
            munmap(const_cast<uint8_t*>(base_), size_);
        }
    }

    bool ItchFileReader::next(const uint8_t*& msg, uint16_t& len){
        if(offset_+2>size_){
            return false;
        }
        len=read_be16(base_+offset_);
        
        if(offset_+2+len>size_){
            return false;
        }
        msg=base_+offset_+2;
        offset_+= 2+len;
        return true;
    }
}