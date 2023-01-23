#ifndef THINGER_ASIO_UNIX_SOCKET_HPP
#define THINGER_ASIO_UNIX_SOCKET_HPP

#include "socket.hpp"

namespace thinger::asio {

    class unix_socket : public socket {

    private:
        boost::asio::local::stream_protocol::socket socket_;

    public:
        unix_socket(const std::string &context, boost::asio::io_service &io_service) : socket(context, io_service), socket_(io_service){

        }

        virtual ~unix_socket() {
            close();
        }

    public:

        std::string get_remote_ip() const override{
            boost::system::error_code ec;
            auto remote_ep = socket_.remote_endpoint(ec);
            if (!ec) {
                return remote_ep.path();
            } else {
                return "";
            }
        }

        std::string get_local_port() const override{
            return "0";
        }

        std::string get_remote_port() const override{
            return "0";
        }

        void cancel() override{
            socket_.cancel();
        }

        void connect(const std::string &path, std::chrono::seconds expire_seconds, ec_handler handler){
            close();

            std::shared_ptr<boost::asio::deadline_timer> timer = std::make_shared<boost::asio::deadline_timer>(get_io_service());
            timer->expires_from_now(boost::posix_time::seconds(expire_seconds.count()));
            timer->async_wait([this](boost::system::error_code ec) {
                if(!ec) {
                    socket_.cancel();
                }
            });

            socket_.async_connect(path, [this, timer, handler](const boost::system::error_code &ec) {
                // cancel timer
                timer->cancel();
                // call handler
                handler(ec);
            });
        }

        void connect(const std::string &host, const std::string &port, std::chrono::seconds expire_seconds, ec_handler handler) override{
            LOG_WARNING("calling connect to a unix socket over host/port");
            connect(host, expire_seconds, handler);
        }

        void close() override{
            try {
                boost::system::error_code ec;
                if (socket_.is_open()) {
                    socket_.close(ec);
                }
            } catch (...) {
            }
        }

        void async_read_some(uint8_t buffer[], size_t max_size,
                                     ec_size_handler handler) override{
            socket_.async_read_some(boost::asio::buffer(buffer, max_size), handler);
        }

        void async_write_some(uint8_t buffer[], size_t size,
                                      ec_size_handler handler) override{
            socket_.async_write_some(boost::asio::buffer(buffer, size), handler);
        }

        void async_write_some(ec_size_handler handler) override{
            socket_.async_write_some(boost::asio::null_buffers(), handler);
        }

        void async_read(uint8_t buffer[], size_t size, ec_size_handler handler) override{
            boost::asio::async_read(socket_, boost::asio::buffer(buffer, size), boost::asio::transfer_exactly(size), handler);
        }

        void async_read(boost::asio::streambuf &buffer, size_t size,
                                ec_size_handler handler) override{
            boost::asio::async_read(socket_, buffer, boost::asio::transfer_exactly(size), handler);
        }

        void async_read_until(boost::asio::streambuf &buffer, const boost::regex &expr,
                                      ec_size_handler handler) override{
            boost::asio::async_read_until(socket_, buffer, expr, handler);
        }

        void async_read_until(boost::asio::streambuf &buffer, const std::string &delim,
                                      ec_size_handler handler) override{
            boost::asio::async_read_until(socket_, buffer, delim, handler);
        }

        void async_write(uint8_t buffer[], size_t size,
                                 ec_size_handler handler) override{
            boost::asio::async_write(socket_, boost::asio::buffer(buffer, size), boost::asio::transfer_exactly(size), handler);
        }

        void async_write(const std::string &str,
                                 ec_size_handler handler) override{
            boost::asio::async_write(socket_, boost::asio::buffer(str), handler);
        }

        void async_write(const std::vector<boost::asio::const_buffer> &buffer,
                                 ec_size_handler handler) override{
            boost::asio::async_write(socket_, buffer, handler);
        }

        bool is_open() const override{
            return socket_.is_open();
        }

        bool is_secure() const override{
            return false;
        }

        size_t read(uint8_t buffer[], size_t size, boost::system::error_code& ec) override{
            return boost::asio::read(socket_,  boost::asio::buffer(buffer, size), ec);
        }

        size_t write(uint8_t buffer[], size_t size, boost::system::error_code& ec) override{
            return boost::asio::write(socket_, boost::asio::const_buffer(buffer, size), ec);
        }

        size_t available() const override{
            boost::system::error_code ec;
            auto size = socket_.available(ec);
            if(ec) LOG_ERROR("error while getting socket available bytes (%zu): %s", size, ec.message().c_str());
            return size;
        }

        void async_wait(boost::asio::socket_base::wait_type type, std::function<void(const boost::system::error_code &e)> handler) override{
            socket_.async_wait(type, std::move(handler));
        }

    };

}

#endif

