#ifndef OUT_SENDFILE_HPP
#define OUT_SENDFILE_HPP

#if __APPLE__
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#elif __linux
#include <sys/sendfile.h>
#endif

#include <boost/asio/error.hpp>
#include "out_data.hpp"
#include "../../sockets/tcp_socket.hpp"

namespace thinger::http::data{

struct sendfile_op
{
    std::shared_ptr<thinger::tcp_socket> sock_;
    int fd_;
    ec_size_handler handler_;
    off_t offset_;
    off_t size_;

    // Function call operator meeting WriteHandler requirements.
    // Used as the handler for the async_write_some operation.
    void operator()(boost::system::error_code ec, std::size_t)
    {
        // Put the underlying socket into non-blocking mode.
        if (!ec && !sock_->get_socket().native_non_blocking()){
            sock_->get_socket().native_non_blocking(true, ec);
        }

        if (!ec)
        {
            for(;;){
                errno = 0;

                #if __APPLE__
                    off_t len = size_ - offset_;
                    int error = ::sendfile(fd_, sock_->get_socket().native_handle(), offset_, &len, NULL, 0);
                    ec = boost::system::error_code(error < 0 ? errno : 0, boost::asio::error::get_system_category());
                    offset_ += len;
                #elif __linux
                    ssize_t n = ::sendfile(sock_->get_socket().native_handle(), fd_, &offset_, size_ - offset_);
                    ec = boost::system::error_code(n < 0 ? errno : 0, boost::asio::error::get_system_category());
                #endif

                // Retry operation immediately if interrupted by signal.
                if (ec == boost::asio::error::interrupted){
                    continue;
                }

                // Check if we need to run the operation again.
                if (ec == boost::asio::error::would_block || ec == boost::asio::error::try_again)
                {
                    // We have to wait for the socket to become ready again.
                    sock_->async_write_some(*this);
                    return;
                }

                if(ec || offset_>=size_){
                    break;
                }
            }
        }

        // Pass result back to user's handler.
        handler_(ec, offset_);
    }
};

class out_sendfile : public out_data{

public:
    out_sendfile(int fd, size_t size) : fd_(fd), size_(size)
    {}

    virtual ~out_sendfile() {
        ::close(fd_);
    }

    virtual bool end_stream(){
        return true;
    }

    virtual size_t get_size(){
        return size_;
    }

    virtual void to_socket(std::shared_ptr<thinger::socket> socket, ec_size_handler handler){
        auto tcp_socket = std::dynamic_pointer_cast<thinger::tcp_socket>(socket);
        if(tcp_socket){
            sendfile_op op = { tcp_socket, fd_, handler, 0, (off_t)size_};
            tcp_socket->async_write_some(op);
        }else{
            LOG_ERROR("cannot use sendfile over non TCP socket");
            handler(boost::system::error_code(boost::system::errc::invalid_argument, boost::asio::error::get_system_category()), 0);
        }
    }

    virtual void to_buffer(std::vector<boost::asio::const_buffer>& buffer){
        LOG_ERROR("cannot append to buffer in sendfile mode");
    }

    virtual bool supports_buffer(){
        return false;
    }

private:
    int fd_;
    size_t size_;

};

}

#endif