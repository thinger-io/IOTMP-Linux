#ifndef THINGER_HTTP_CLIENT_CONNECTION_HPP
#define THINGER_HTTP_CLIENT_CONNECTION_HPP

#include <functional>
#include <queue>
#include <boost/noncopyable.hpp>
#include <memory>

#include "http_request.hpp"
#include "http_response.hpp"
#include "http_response_factory.hpp"
#include "../asio/sockets/tcp_socket.hpp"
#include "../asio/sockets/unix_socket.hpp"
#include "../util/shared_keeper.hpp"

namespace thinger::http{

    class http_client_connection : public std::enable_shared_from_this<http_client_connection>, public boost::noncopyable {

        static const int MAX_RETRIES = 3;

        /**
          * Constant for controlling the maximum number of bytes used in the incoming
          * buffer for each HTTP connection.
          */
        static const unsigned MAX_BUFFER_SIZE = 4096;

        /**
         * Constant for controlling the maximum number of pending requests stored
         * in the output queue.
         */
        static const unsigned MAX_OUTPUT_REQUESTS = 256;

        /**
         * Parameter for controlling the connection timeout. Number of seconds without
         * data transfer to close the connection.
         */
        static constexpr auto CLIENT_CONNECTION_TIMEOUT_SECONDS = std::chrono::seconds{60};

        /**
         * Parameter for controlling the connection timeout. Number of seconds without
         * data transfer to close the connection.
         */
        static constexpr auto SOCKET_CONNECTION_TIMEOUT_SECONDS = std::chrono::seconds{10};

    public:
        /**
         * Parameter for controlling the number of live http client connections
         */
        static std::atomic<unsigned long> connections;

        /**
         * Constructor based on a socket
         */
        explicit http_client_connection(std::shared_ptr<thinger::asio::socket> socket);

        /**
         * Constructor based on a unix socket
         */
        http_client_connection(std::shared_ptr<thinger::asio::unix_socket> socket, const std::string& path);

        /**
         * Destructor
         */
        virtual ~http_client_connection();

    private:

        /**
         * Handle socket connection (i.e. reconnect)
         */
        void handle_socket_connection(boost::system::error_code ec = boost::system::error_code{});

        /**
         * Handle error
         * @param e
         */
        void handle_error(const boost::system::error_code &e);

        /**
         * Handle response reading
         * @param e
         */
        void handle_response(const boost::system::error_code &e);

        /**
         * Read HTTP response from socket
         */
        void read_response();

        /**
         * Handle output queue
         */
        void handle_output_queue();

        /**
         * Will keep the instance (connection) alive while waiting for more requests
         */
        void start();

    public:

        /**
        * Method to send a HTTP request. Can be called from any thread.
        */
        void send_request(std::shared_ptr<http_request> request,
                          std::function<void(const boost::system::error_code &,
                                             std::shared_ptr<http_response>)> handler);

        /**
        * Return the base socket used in this client connection and release it for being used by another connection handler.
        * No further calls mut be done to this instance.
        */
        std::shared_ptr<thinger::asio::socket> release_socket();

    private:
        /// Shared pointer socket
        std::shared_ptr<thinger::asio::socket> socket_;

        /// Socket path for unix sockets
        std::string socket_path_;

        /// For timeout the connection
        std::shared_ptr<thinger::util::shared_keeper<http_client_connection>> shared_keeper_;

        /// Buffer for reading incoming data.
        uint8_t buffer_[MAX_BUFFER_SIZE];

        /// HTTP response factory. Parse http responses from bytes
        http_response_factory response_parser_;

        /// Number of connection retries
        unsigned short current_retries_ = 0;

        /// Queue for storing request over the connection
        std::queue<std::pair<std::shared_ptr<http_request>, std::function<void(const boost::system::error_code &, std::shared_ptr<http_response>)>>> request_queue_;
    };

}

#endif