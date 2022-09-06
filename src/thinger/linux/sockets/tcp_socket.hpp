#ifndef THINGER_TCP_SOCKET_HPP
#define THINGER_TCP_SOCKET_HPP

#include <memory>
#include <functional>
#include <thread>
#include <mutex>
#include <iostream>

#include <boost/asio.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/regex.hpp>
#include <loguru.hpp>

#include "socket.hpp"

namespace thinger {

    class tcp_socket : public socket {

    protected:
        boost::asio::ip::tcp::socket socket_;

    public:
        tcp_socket(const std::string &context, boost::asio::io_service &io_service) : socket(context, io_service), socket_(io_service) {

        }

        tcp_socket(const std::string &context, const std::shared_ptr<tcp_socket>& tcp_socket) : socket(context, tcp_socket->get_io_service()), socket_(std::move(tcp_socket->get_socket())) {

        }

        virtual ~tcp_socket() {
            LOG_F(3, "releasing tcp connection");
            close();
        }

    public:


    public:

        void connect(const std::string &host, const std::string &port, std::chrono::seconds expire_seconds,
                             std::function<void(const boost::system::error_code &)> handler) override {
            close();

            // resolve socket host
            std::shared_ptr<boost::asio::ip::tcp::resolver> resolver = std::make_shared<boost::asio::ip::tcp::resolver>(get_io_service());
            boost::asio::ip::tcp::resolver::query query(host, port);

            resolver->async_resolve(query, [this, host, expire_seconds, handler, resolver](const boost::system::error_code &ec,
                                                                                           boost::asio::ip::tcp::resolver::iterator it) {
                if (ec) {
                    return handler(ec);
                }

                std::shared_ptr<boost::asio::deadline_timer> timer = std::make_shared<boost::asio::deadline_timer>(get_io_service());
                timer->expires_from_now(boost::posix_time::seconds(expire_seconds.count()));
                timer->async_wait([this](boost::system::error_code ec) {
                    if (!ec) {
                        socket_.cancel();
                    }
                });

                // connect socket
                boost::asio::async_connect(get_socket(), it, [this, host, timer, handler](const boost::system::error_code &ec,
                                                                                          boost::asio::ip::tcp::resolver::iterator it) {
                    // cancel timer
                    timer->cancel();

                    // check any errors to return
                    if (ec) {
                        return handler(ec);
                    }

                    // handle handshake
                    if (requires_handshake()) {
                        run_handshake([this, handler](const boost::system::error_code &ec) {
                            if (ec) {
                            	LOG_F(ERROR, "error while running SSL/TLS handshake: %s", ec.message().c_str());
                            }
                            handler(ec);
                        }, host);
                    } else {
                        // just provide the provided ec (no error)
                        handler(ec);
                    }
                });
            });
        }

        virtual boost::asio::ip::tcp::socket &get_socket(){
            return socket_;
        }

        std::string get_remote_ip() const override{
            boost::system::error_code ec;
            boost::asio::ip::tcp::endpoint remote_ep = socket_.remote_endpoint(ec);
            if (!ec) {
                boost::asio::ip::address remote_ad = remote_ep.address();
                return remote_ad.to_string();
            } else {
                // todo review this constant, look at default ip address in boost socket ipv4/ipv6
                return "0.0.0.0";
            }
        }

        std::string get_local_port() const override{
            boost::system::error_code ec;
            boost::asio::ip::tcp::endpoint local_ep = socket_.local_endpoint(ec);
            if (!ec) {
                return std::to_string(local_ep.port());
            } else {
                return "0";
            }
        }

        std::string get_remote_port() const override {
            boost::system::error_code ec;
            boost::asio::ip::tcp::endpoint remote_ep = socket_.remote_endpoint(ec);
            if (!ec) {
                return std::to_string(remote_ep.port());
            } else {
                return "0";
            }
        }

        void cancel() override {
            socket_.cancel();
        }

        /**
        * Indicate whether this socket requires a handshake, i.e., for an SSL connection. If it
        * returns true, then run_connection_handshake will be called;
        */
        bool requires_handshake() const override {
            return false;
        }

        /**
         * Called when the socket is connected an initialized. Must be implemented by
         * underlying socket, i.e., a SSL Socket must start the handshake in this moment.
         */
        void run_handshake(std::function<void(const boost::system::error_code &error)> callback, const std::string &host = "") override {
            static const auto no_error = boost::system::error_code();
            callback(no_error);
        }

        /**
        * Can be called to close the actual socket connection in a graceful way.
        */
        void close() override;

        void async_read_some(uint8_t buffer[], size_t max_size,
                                     std::function<void(const boost::system::error_code &e, std::size_t bytes_transferred)> handler) override{
            socket_.async_read_some(boost::asio::buffer(buffer, max_size), handler) ;
        }

        void async_write_some(uint8_t buffer[], size_t size,
                                      std::function<void(const boost::system::error_code &e, std::size_t bytes_transferred)> handler) override {
            socket_.async_write_some(boost::asio::buffer(buffer, size), handler);
        }

        void async_write_some(std::function<void(const boost::system::error_code &e, std::size_t bytes_transferred)> handler) override {
            socket_.async_write_some(boost::asio::null_buffers(), handler);
        }

        void async_read(uint8_t buffer[], size_t size,
                                std::function<void(const boost::system::error_code &e, std::size_t bytes_transferred)> handler) override{
            boost::asio::async_read(socket_, boost::asio::buffer(buffer, size), boost::asio::transfer_exactly(size), handler);
        }

        void async_read(boost::asio::streambuf &buffer, size_t size,
                                std::function<void(const boost::system::error_code &e, std::size_t bytes_transferred)> handler) override{
            boost::asio::async_read(socket_, buffer, boost::asio::transfer_exactly(size), handler);
        }

        void async_read_until(boost::asio::streambuf &buffer, const boost::regex &expr,
                                      std::function<void(const boost::system::error_code &e, std::size_t bytes_transferred)> handler) override{
            boost::asio::async_read_until(socket_, buffer, expr, handler);
        }

        void async_read_until(boost::asio::streambuf &buffer, const std::string &delim,
                                      std::function<void(const boost::system::error_code &e, std::size_t bytes_transferred)> handler) override{
            boost::asio::async_read_until(socket_, buffer, delim, handler);
        }

        void async_write(uint8_t buffer[], size_t size,
                                 std::function<void(const boost::system::error_code &e, std::size_t bytes_transferred)> handler) override{
            boost::asio::async_write(socket_, boost::asio::buffer(buffer, size), boost::asio::transfer_exactly(size), handler);
        }

        void async_write(const std::string &str,
                                 std::function<void(const boost::system::error_code &e, std::size_t bytes_transferred)> handler) override{
            boost::asio::async_write(socket_, boost::asio::buffer(str), handler);
        }

        void async_write(const std::vector<boost::asio::const_buffer> &buffer,
                                 std::function<void(const boost::system::error_code &e, std::size_t bytes_transferred)> handler) override{
            boost::asio::async_write(socket_, buffer, handler);
        }

        void enable_tcp_no_delay() {
            boost::asio::ip::tcp::no_delay option(true);
            socket_.set_option(option);
        }

        void disable_tcp_no_delay() {
            boost::asio::ip::tcp::no_delay option(false);
            socket_.set_option(option);
        }

        bool is_open() const override{
            return socket_.is_open();
        }

        bool is_secure() const override{
            return false;
        }
    };

}

#endif
