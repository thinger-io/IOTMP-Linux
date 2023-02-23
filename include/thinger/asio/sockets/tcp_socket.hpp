#ifndef THINGER_ASIO_TCP_SOCKET_HPP
#define THINGER_ASIO_TCP_SOCKET_HPP

#include <memory>
#include <functional>
#include <thread>
#include <mutex>
#include <iostream>

#include <boost/asio.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/regex.hpp>

#include "../../util/logger.hpp"
#include "socket.hpp"

namespace thinger::asio {

class tcp_socket : public socket {

public:
    // constructors and destructors
    tcp_socket(const std::string &context, boost::asio::io_service &io_service);
    tcp_socket(const std::string &context, const std::shared_ptr<tcp_socket>& tcp_socket);
    ~tcp_socket() override;

    // socket control
    void connect(const std::string &host, const std::string &port, std::chrono::seconds expire_seconds, ec_handler handler) override;
    void close() override;
    void cancel() override;

    // read
    size_t read(uint8_t buffer[], size_t size, boost::system::error_code& ec) override;
    void async_read_some(uint8_t buffer[], size_t max_size, ec_size_handler handler) override;
    void async_write_some(uint8_t buffer[], size_t size,  ec_size_handler handler) override;
    void async_write_some(ec_size_handler handler) override;
    void async_read(uint8_t buffer[], size_t size, ec_size_handler handler) override;
    void async_read(boost::asio::streambuf &buffer, size_t size, ec_size_handler handler) override;
    void async_read_until(boost::asio::streambuf &buffer, const boost::regex &expr, ec_size_handler handler) override;
    void async_read_until(boost::asio::streambuf &buffer, const std::string &delim, ec_size_handler handler) override;

    // write
    size_t write(uint8_t buffer[], size_t size, boost::system::error_code& ec) override;
    void async_write(uint8_t buffer[], size_t size, ec_size_handler handler) override;
    void async_write(const std::string &str, ec_size_handler handler) override;
    void async_write(const std::vector<boost::asio::const_buffer> &buffer, ec_size_handler handler) override;

    // wait
    void async_wait(boost::asio::socket_base::wait_type type, ec_handler handler) override;

    // some getters to check the state
    bool is_open() const override;
    bool is_secure() const override;
    size_t available() const override;
    std::string get_remote_ip() const override;
    std::string get_local_port() const override;
    std::string get_remote_port() const override;

    // other methods
    void enable_tcp_no_delay();
    void disable_tcp_no_delay();
    virtual boost::asio::ip::tcp::socket &get_socket();

protected:
    boost::asio::ip::tcp::socket socket_;
    bool time_out_ = false;

};

}

#endif
