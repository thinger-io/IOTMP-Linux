#ifndef THINGER_UNIX_SOCKET_HPP
#define THINGER_UNIX_SOCKET_HPP

#include "socket.hpp"

namespace thinger {

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

        virtual std::string get_remote_ip() const{
            boost::system::error_code ec;
            auto remote_ep = socket_.remote_endpoint(ec);
            if (!ec) {
                return remote_ep.path();
            } else {
                return "";
            }
        }

        virtual std::string get_local_port() const{
            return "0";
        }

        virtual std::string get_remote_port() const{
            return "0";
        }

        virtual void cancel(){
            socket_.cancel();
        }

        void connect(const std::string &path, std::chrono::seconds expire_seconds, std::function<void(const boost::system::error_code &)> handler){
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

        virtual void connect(const std::string &host, const std::string &port, std::chrono::seconds expire_seconds, std::function<void(const boost::system::error_code &)> handler){
            LOG_F(WARNING, "calling connect to a unix socket over host/port");
            connect(host, expire_seconds, handler);
        }

        virtual void close(){
            try {
                boost::system::error_code ec;
                if (socket_.is_open()) {
                    socket_.close(ec);
                }
            } catch (...) {
            }
        }

        virtual void async_read_some(uint8_t buffer[], size_t max_size,
                                     std::function<void(const boost::system::error_code &e, std::size_t bytes_transferred)> handler){
            socket_.async_read_some(boost::asio::buffer(buffer, max_size), handler);
        }

        virtual void async_write_some(uint8_t buffer[], size_t size,
                                      std::function<void(const boost::system::error_code &e, std::size_t bytes_transferred)> handler){
            socket_.async_write_some(boost::asio::buffer(buffer, size), handler);
        }

        virtual void async_write_some(std::function<void(const boost::system::error_code &e, std::size_t bytes_transferred)> handler){
            socket_.async_write_some(boost::asio::null_buffers(), handler);
        }

        virtual void async_read(uint8_t buffer[], size_t size, std::function<void(const boost::system::error_code &e, std::size_t bytes_transferred)> handler){
            boost::asio::async_read(socket_, boost::asio::buffer(buffer, size), boost::asio::transfer_exactly(size), handler);
        }

        virtual void async_read(boost::asio::streambuf &buffer, size_t size,
                                std::function<void(const boost::system::error_code &e, std::size_t bytes_transferred)> handler){
            boost::asio::async_read(socket_, buffer, boost::asio::transfer_exactly(size), handler);
        }

        virtual void async_read_until(boost::asio::streambuf &buffer, const boost::regex &expr,
                                      std::function<void(const boost::system::error_code &e, std::size_t bytes_transferred)> handler){
            boost::asio::async_read_until(socket_, buffer, expr, handler);
        }

        virtual void async_read_until(boost::asio::streambuf &buffer, const std::string &delim,
                                      std::function<void(const boost::system::error_code &e, std::size_t bytes_transferred)> handler){
            boost::asio::async_read_until(socket_, buffer, delim, handler);
        }

        virtual void async_write(uint8_t buffer[], size_t size,
                                 std::function<void(const boost::system::error_code &e, std::size_t bytes_transferred)> handler){
            boost::asio::async_write(socket_, boost::asio::buffer(buffer, size), boost::asio::transfer_exactly(size), handler);
        }

        virtual void async_write(const std::string &str,
                                 std::function<void(const boost::system::error_code &e, std::size_t bytes_transferred)> handler){
            boost::asio::async_write(socket_, boost::asio::buffer(str), handler);
        }

        virtual void async_write(const std::vector<boost::asio::const_buffer> &buffer,
                                 std::function<void(const boost::system::error_code &e, std::size_t bytes_transferred)> handler){
            boost::asio::async_write(socket_, buffer, handler);
        }

        virtual bool is_open() const {
            return socket_.is_open();
        }

        virtual bool is_secure() const {
            return false;
        }
    };

}

#endif

