#include <iostream>
#include "socket.hpp"

namespace thinger_client {

    void socket::close()
    {
        boost::system::error_code ec;
        if(socket_.is_open()){
            socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
        }
        socket_.close(ec);
    }

}