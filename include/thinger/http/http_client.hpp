#ifndef THINGER_HTTP_CLIENT_HPP
#define THINGER_HTTP_CLIENT_HPP

#include "http_request.hpp"
#include "http_response.hpp"
#include "http_client_connection.hpp"
#include <memory>
#include <functional>

namespace thinger::http {

    static const unsigned int MAX_REQUEST_HOPS = 5;

    void send_request(const std::shared_ptr<http_request>& request,
                      const std::shared_ptr<http_client_connection>& connection,
                      std::function<void(const boost::system::error_code&, std::shared_ptr<http_response>)> handler,
                      unsigned int hops=0);

    void send_request(const std::shared_ptr<http_request>& request,
                      const std::function<void(const boost::system::error_code&, std::shared_ptr<http_response>)>& handler,
                      unsigned int hops=0);

    void send_request(const std::shared_ptr<http_request>& request,
                      std::function<void(const boost::system::error_code&,
                                         std::shared_ptr<http_client_connection>,
                                         std::shared_ptr<http_response>)> handler);

    void send_request(boost::asio::io_service& io_service,
                      const std::shared_ptr<http_request>& request,
                      std::function<void(const boost::system::error_code&,
                                         std::shared_ptr<http_client_connection>,
                                         std::shared_ptr<http_response>)> handler);

    void send_request(boost::asio::io_service& io_service,
                      const std::shared_ptr<http_request>& request,
                      const std::function<void(const boost::system::error_code&, std::shared_ptr<http_response>)>& handler,
                      unsigned int hops=0);

}

#endif
