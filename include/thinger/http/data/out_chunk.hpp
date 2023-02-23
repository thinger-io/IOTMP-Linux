#ifndef OUT_CHUNK_HPP
#define OUT_CHUNK_HPP

#include <boost/format.hpp>
#include "out_data.hpp"

namespace thinger{
namespace server{
namespace base{

class out_chunk : public out_data{

public:
    out_chunk(const std::string& str) : str_(str){
        size_ = (boost::format("%x") % str_.size()).str();
    }

    out_chunk() : str_(""), size_("0"){
    }

    virtual ~out_chunk() {
    }

    std::string& get_string(){
        return str_;
    }

    virtual size_t get_size(){
        return str_.size();
    }

protected:

    virtual void to_socket(std::shared_ptr<base::socket> socket, ec_size_handler handler){
        std::vector<boost::asio::const_buffer> buffer;
        buffer.push_back(boost::asio::buffer(size_));
        buffer.push_back(boost::asio::buffer(http::misc_strings::crlf));
        buffer.push_back(boost::asio::buffer(str_));
        buffer.push_back(boost::asio::buffer(http::misc_strings::crlf));
        socket->async_write(buffer, handler);
    }

    virtual void to_buffer(std::vector<boost::asio::const_buffer>& buffer){
        buffer.push_back(boost::asio::buffer(size_));
        buffer.push_back(boost::asio::buffer(http::misc_strings::crlf));
        buffer.push_back(boost::asio::buffer(str_));
        buffer.push_back(boost::asio::buffer(http::misc_strings::crlf));
    }

private:
    std::string str_;
    std::string size_;
};

}
}
}

#endif