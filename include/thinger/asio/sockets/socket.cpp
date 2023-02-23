#include <iostream>
#include "socket.hpp"

namespace thinger::asio {

    std::atomic<unsigned long> socket::connections(0);

    socket::socket(const std::string& context, boost::asio::io_service& io_service) : io_service_(io_service), context_(context) {
        connections++;
        std::unique_lock<std::mutex> objectLock(mutex_, std::try_to_lock);
        context_count[context_]++;
    }

    socket::~socket(){
        connections--;
        std::unique_lock<std::mutex> objectLock(mutex_, std::try_to_lock);
        context_count[context_]--;
    }

    boost::asio::io_service& socket::get_io_service(){
        return io_service_;
    }

    bool socket::requires_handshake() const{
        return false;
    }

    void socket::run_handshake(ec_handler callback, const std::string& host){
        callback(boost::system::error_code{});
    }

    std::map<std::string, unsigned long> socket::context_count;
    std::mutex socket::mutex_;
}