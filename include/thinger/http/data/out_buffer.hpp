#ifndef THINGER_HTTP_OUT_BUFFER_HPP
#define THINGER_HTTP_OUT_BUFFER_HPP

#include "out_data.hpp"

namespace thinger::http::data{

class out_buffer : public out_data{

public:
    out_buffer(uint8_t* buffer, size_t size, bool copy_buffer=false) :
            size_(size), release_memory_(copy_buffer)
    {
        if(copy_buffer){
            buffer_ = new uint8_t[size];
            std::memcpy(buffer_, buffer, size);
        }else{
            buffer_ = buffer;
        }
    }

    virtual ~out_buffer() {
        if(release_memory_){
            delete[] buffer_;
        }
    }

    virtual size_t get_size(){
        return size_;
    }


protected:

    /*
    virtual void to_socket(std::shared_ptr<base::socket> socket, ec_size_handler handler){
        socket->async_write(buffer_, size_, handler);
    }*/

    virtual void to_buffer(std::vector<boost::asio::const_buffer>& buffer){
        buffer.push_back(boost::asio::const_buffer(buffer_, size_));
    }


private:
    bool release_memory_;
    uint8_t* buffer_;
    size_t size_;
};

}

#endif