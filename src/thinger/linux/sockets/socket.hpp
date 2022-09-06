#ifndef THINGER_SOCKET_HPP
#define THINGER_SOCKET_HPP

#include <memory>
#include <functional>
#include <thread>
#include <mutex>
#include <iostream>

#include <boost/asio.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/regex.hpp>

namespace thinger{

class socket : private boost::asio::noncopyable {

    protected:
        std::string context_;
        boost::asio::io_service &io_service_;

    public:
        socket(const std::string &context, boost::asio::io_service &io_service) : io_service_(io_service), context_(context) {
            connections++;
            std::unique_lock<std::mutex> objectLock(mutex_, std::try_to_lock);
            context_count[context_]++;
        }

        virtual ~socket() {
            connections--;
            std::unique_lock<std::mutex> objectLock(mutex_, std::try_to_lock);
            context_count[context_]--;
        }

        static std::atomic<unsigned long> connections;
        static std::map<std::string, unsigned long> context_count;
        static std::mutex mutex_;

    public:

        boost::asio::io_service &get_io_service() {
            return io_service_;
        }

        virtual std::string get_remote_ip() const = 0;

        virtual std::string get_local_port() const = 0;

        virtual std::string get_remote_port() const = 0;

        /**
         * Canel pending async tasks
         */
        virtual void cancel() = 0;

        /**
        * Indicate whether this socket requires a handshake, i.e., for an SSL connection. If it
        * returns true, then run_connection_handshake will be called;
        */
        virtual bool requires_handshake() const {
            return false;
        }

        /**
         * Called when the socket is connected an initialized. Must be implemented by
         * underlying socket, i.e., a SSL Socket must start the handshake in this moment.
         */
        virtual void run_handshake(std::function<void(const boost::system::error_code &error)> callback, const std::string &host = "") {
            static const auto no_error = boost::system::error_code();
            callback(no_error);
        }

        virtual void connect(const std::string &host, const std::string &port, std::chrono::seconds expire_seconds,
                             std::function<void(const boost::system::error_code &)> handler) = 0;

        /**
        * Can be called to close the actual socket connection in a graceful way.
        */
        virtual void close() = 0;


        virtual void async_read_some(uint8_t buffer[], size_t max_size,
                                     std::function<void(const boost::system::error_code &e, std::size_t bytes_transferred)> handler) = 0;

        virtual void async_write_some(uint8_t buffer[], size_t size,
                                      std::function<void(const boost::system::error_code &e, std::size_t bytes_transferred)> handler) = 0;

        virtual void async_write_some(std::function<void(const boost::system::error_code &e, std::size_t bytes_transferred)> handler) = 0;

        virtual void async_read(uint8_t buffer[], size_t size,
                                std::function<void(const boost::system::error_code &e, std::size_t bytes_transferred)> handler) = 0;

        virtual void async_read(boost::asio::streambuf &buffer, size_t size,
                                std::function<void(const boost::system::error_code &e, std::size_t bytes_transferred)> handler) = 0;

        virtual void async_read_until(boost::asio::streambuf &buffer, const boost::regex &expr,
                                      std::function<void(const boost::system::error_code &e, std::size_t bytes_transferred)> handler) = 0;

        virtual void async_read_until(boost::asio::streambuf &buffer, const std::string &delim,
                                      std::function<void(const boost::system::error_code &e, std::size_t bytes_transferred)> handler) = 0;

        virtual void async_write(uint8_t buffer[], size_t size,
                                 std::function<void(const boost::system::error_code &e, std::size_t bytes_transferred)> handler) = 0;

        virtual void async_write(const std::string &str,
                                 std::function<void(const boost::system::error_code &e, std::size_t bytes_transferred)> handler) = 0;

        virtual void async_write(const std::vector<boost::asio::const_buffer> &buffer,
                                 std::function<void(const boost::system::error_code &e, std::size_t bytes_transferred)> handler) = 0;

        virtual bool is_open() const = 0;

        virtual bool is_secure() const = 0;
    };

}

#endif /* SOCKET_HPP_ */
