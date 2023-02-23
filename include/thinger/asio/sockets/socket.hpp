
#ifndef THINGER_ASIO_SOCKET_HPP
#define THINGER_ASIO_SOCKET_HPP

#include <memory>
#include <functional>
#include <thread>
#include <mutex>
#include <iostream>

#include <boost/asio.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/regex.hpp>

#include "../../util/types.hpp"

namespace thinger::asio{

class socket : private boost::asio::noncopyable {

public:
    // constructors and destructors
    socket(const std::string &context, boost::asio::io_service &io_service);
    virtual ~socket();

    // socket control
    virtual void connect(const std::string &host, const std::string &port, std::chrono::seconds expire_seconds, ec_handler handler) = 0;
    virtual void close() = 0;
    virtual void cancel() = 0;
    virtual bool requires_handshake() const;
    virtual void run_handshake(ec_handler callback, const std::string &host = "");

    // read
    virtual size_t read(uint8_t buffer[], size_t size, boost::system::error_code& ec) = 0;
    virtual void async_read_some(uint8_t buffer[], size_t max_size, ec_size_handler handler) = 0;
    virtual void async_write_some(uint8_t buffer[], size_t size, ec_size_handler handler) = 0;
    virtual void async_write_some(ec_size_handler handler) = 0;
    virtual void async_read(uint8_t buffer[], size_t size, ec_size_handler handler) = 0;
    virtual void async_read(boost::asio::streambuf &buffer, size_t size, ec_size_handler handler) = 0;
    virtual void async_read_until(boost::asio::streambuf &buffer, const boost::regex &expr, ec_size_handler handler) = 0;
    virtual void async_read_until(boost::asio::streambuf &buffer, const std::string &delim, ec_size_handler handler) = 0;

    // write
    virtual size_t write(uint8_t buffer[], size_t size, boost::system::error_code& ec) = 0;
    virtual void async_write(uint8_t buffer[], size_t size, ec_size_handler handler) = 0;
    virtual void async_write(const std::string &str, ec_size_handler handler) = 0;
    virtual void async_write(const std::vector<boost::asio::const_buffer> &buffer, ec_size_handler handler) = 0;

    // wait
    virtual void async_wait(boost::asio::socket_base::wait_type type, ec_handler) = 0;

    // some getters to check the state
    virtual bool is_open() const = 0;
    virtual bool is_secure() const = 0;
    virtual size_t available() const = 0;
    virtual std::string get_remote_ip() const = 0;
    virtual std::string get_local_port() const = 0;
    virtual std::string get_remote_port() const = 0;

    // other methods
    boost::asio::io_service &get_io_service();

protected:
    std::string context_;
    boost::asio::io_service &io_service_;
    static std::atomic<unsigned long> connections;
    static std::map<std::string, unsigned long> context_count;
    static std::mutex mutex_;
};

}

#endif
