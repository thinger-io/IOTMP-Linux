#ifndef THINGER_TYPES
#define THINGER_TYPES

#include <functional>
#include <boost/asio.hpp>

namespace thinger{
    using ec_size_handler = std::function<void(const boost::system::error_code& e, std::size_t bytes_transferred)>;;
    using ec_handler = std::function<void(const boost::system::error_code& e)>;
}

#endif