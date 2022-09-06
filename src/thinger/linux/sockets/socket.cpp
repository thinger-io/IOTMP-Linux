#include <iostream>
#include "socket.hpp"

namespace thinger {

    std::atomic<unsigned long> socket::connections(0);
    std::map<std::string, unsigned long> socket::context_count;
    std::mutex socket::mutex_;

}