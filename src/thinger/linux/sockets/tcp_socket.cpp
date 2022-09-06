#include <iostream>
#include "tcp_socket.hpp"

namespace thinger {

    void tcp_socket::close() {
	    boost::system::error_code ec;
	    if(socket_.is_open()){
		    socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
	    }
        socket_.close(ec);
        LOG_F(3, "closing tcp socket result: %s", ec.message().c_str());
    }

}