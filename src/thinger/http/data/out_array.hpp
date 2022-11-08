#ifndef OUT_ARRAY_HPP
#define OUT_ARRAY_HPP

#include "out_data.hpp"

namespace thinger::http::data{

class out_array : public out_data{

public:
    out_array(size_t size) :
            data_(size)
    {
    }

    virtual ~out_array() {
    }

protected:

    virtual void to_buffer(std::vector<boost::asio::const_buffer>& buffer){
        buffer.push_back(boost::asio::buffer(data_));
    }

public:

    char * get_array(){
        return &data_[0];
    }

    virtual size_t get_size(){
        return data_.size();
    }

private:
    std::vector<char> data_;
};

}

#endif