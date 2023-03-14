#include "http_client_connection.hpp"
#include "../util/logger.hpp"
#include <boost/asio/ssl.hpp>
#include <utility>

namespace thinger::http {
    std::atomic<unsigned long> http_client_connection::connections(0);

    http_client_connection::http_client_connection(std::shared_ptr<thinger::asio::socket> socket) :
        socket_(std::move(socket))
    {
        connections++;
        LOG_LEVEL(3, "created http client connection. total: %u", unsigned(connections));
    }

    http_client_connection::http_client_connection(std::shared_ptr<thinger::asio::unix_socket> socket, const std::string& path) :
        socket_(std::move(socket)),
        socket_path_(path)
    {
        connections++;
        LOG_LEVEL(3, "created http client connection (for unix socket: %s). total: %u", path.c_str(), unsigned(connections));
    }

    http_client_connection::~http_client_connection() {
        connections--;
        LOG_LEVEL(3, "releasing http client connection. total: %u", unsigned(connections));
    }

    void http_client_connection::handle_socket_connection(boost::system::error_code e) {
        // only reconnect should be necessary if there are pending requests
        if (request_queue_.empty()) return;

        // get front request
        auto request = request_queue_.front().first;

        LOG_LEVEL(2, "connecting to: %s:%s (attempt #%d)", request->get_host().c_str(), request->get_port().c_str(), current_retries_);

        if (current_retries_ >= MAX_RETRIES) {
            LOG_ERROR("aborting http request. cannot connect to server. tried: %d times", current_retries_);
            return handle_error(e);
        }

        // increase current retries
        current_retries_++;

        // reset socket
        if(socket_->is_open()){
            socket_->close();
        }

        auto connect_callback = [request, self = shared_from_this(), this](const boost::system::error_code &e) {
            if (e || !socket_->is_open()) {
                if(socket_path_.empty()){
                    LOG_ERROR("error while connecting to '%s'. error: %s (%d) (%s) (attempt #%d)", request->get_host().c_str(), e.message().c_str(), e.value(), e.category().name(), current_retries_);
                }else{
                    LOG_ERROR("error while connecting to '%s'. error: %s (%d) (%s) (attempt #%d)", socket_path_.c_str(), e.message().c_str(), e.value(), e.category().name(), current_retries_);
                }
                // try to reconnect
                // operation aborted: local socket close
                // host not found: host is not available
                // file not found (for unix sockets)
                // problem with ssl certificate
                if( e!=boost::asio::error::operation_aborted &&
                    e!=boost::asio::error::host_not_found &&
                    e.category() != boost::asio::error::get_ssl_category()){
                    return handle_socket_connection(e);
                }
                return handle_error(e);
            }

            LOG_LEVEL(2, "connection established");

            // start shared keeper after the connection is alive!
            start();

            // handle output queue
            handle_output_queue();
        };

        if(socket_path_.empty()){
            // start connection
            socket_->connect(request->get_host(), request->get_port(), SOCKET_CONNECTION_TIMEOUT_SECONDS, connect_callback);
        }else{
            auto socket = std::static_pointer_cast<thinger::asio::unix_socket>(socket_);
            socket->connect(socket_path_, SOCKET_CONNECTION_TIMEOUT_SECONDS, connect_callback);
        }
    }

    void http_client_connection::handle_error(const boost::system::error_code &e) {
        // reset number of retries
        current_retries_ = 0;

        // close connection to avoid affecting pending requests
        if (socket_->is_open()) {
            socket_->close();
        }

        // reset response parser
        response_parser_.reset();

        // notify error to current request (if any)
        if (!request_queue_.empty()) {
            request_queue_.front().second(e, std::shared_ptr<http_response>());
            request_queue_.pop();
        }

        // if there are pending requests, try to handle them
        if(!request_queue_.empty()) {
            handle_output_queue();

        // else... instance is no longer necessary
        }else if(shared_keeper_){
            shared_keeper_->clear();
        }
    }

    void http_client_connection::handle_response(const boost::system::error_code &e) {
        // reset number of retries
        current_retries_ = 0;

        auto response = response_parser_.consume_response();

        if (!request_queue_.empty()) {
            // call handler
            request_queue_.front().second(e, response);

            // remove request from queue
            request_queue_.pop();

            // close the connection if it is not being keep alive by the server
            if (!response->keep_alive()) {
                socket_->close();
            }

            // keep handling output queue if there are pending requests
            if (!request_queue_.empty()) {
                handle_output_queue();

            // request queue empty and not keep alive -> clear http client connection
            }else if(!response->keep_alive()){
                shared_keeper_->clear();
            }

        } else {
            LOG_ERROR("response available but not pending request. this is so weird");
            socket_->close();
            shared_keeper_->clear();
        }
    }

    void http_client_connection::read_response() {
        socket_->async_read_some(buffer_, MAX_BUFFER_SIZE, [this, self = shared_from_this()](const boost::system::error_code &e,  std::size_t bytes_transferred) {
            if(e) {
                LOG_ERROR("error while reading socket response: %s", e.message().c_str());
                return handle_socket_connection(e);
            }

            // keep connection open
            shared_keeper_->heartbeat();

            // determine if we are expecting a head request (
            bool head_request = request_queue_.front().first->get_method()==http::method::HEAD;

            // try to parse response
            boost::tribool result = response_parser_.parse(buffer_, buffer_ + bytes_transferred, head_request);

            // successful read response
            if (result) {
                handle_response(e);
            }
            // bad response
            else if (!result) {
                handle_error(boost::asio::error::invalid_argument);
            }
            // still not received whole request, keep reading
            else {
                read_response();
            }
        });
    }

    void http_client_connection::handle_output_queue() {
        if(request_queue_.empty()) return;
        auto request = request_queue_.front().first;
        request->log("HTTP CLIENT REQUEST", 0);
        request->to_socket(socket_, [this, self = shared_from_this(), request](const boost::system::error_code &e, std::size_t bytes_transferred) {
            if (e) {
                LOG_ERROR("error while writing to socket: %s", e.message().c_str());
                // try to reconnect
                return handle_socket_connection(e);
            }

            // keep connection alive
            shared_keeper_->heartbeat();

            response_parser_.setOnChunked(request->get_chunked_callback());

            // read response
            read_response();
        });
    }

    void http_client_connection::start() {
        if (!shared_keeper_) {
            shared_keeper_ = std::make_shared<thinger::util::shared_keeper<http_client_connection>>(socket_->get_io_service());
            // start shared keeper to avoid instance drop
            shared_keeper_->keep(shared_from_this(), [this]() {
                LOG_LEVEL(2, "http client connection timed out after %" PRId64 " seconds", CLIENT_CONNECTION_TIMEOUT_SECONDS.count());
                handle_error(boost::asio::error::timed_out);
            }, CLIENT_CONNECTION_TIMEOUT_SECONDS);
        }else{
            if(shared_keeper_->timed_out()){
                shared_keeper_->keep(shared_from_this(), [this]() {
                    LOG_LEVEL(2, "http client connection timed out after %" PRId64 " seconds", CLIENT_CONNECTION_TIMEOUT_SECONDS.count());
                    handle_error(boost::asio::error::timed_out);
                }, CLIENT_CONNECTION_TIMEOUT_SECONDS);
            }else{
                shared_keeper_->heartbeat();
            }
        }
    }

    void http_client_connection::send_request(std::shared_ptr<http_request> request, std::function<void(const boost::system::error_code &, std::shared_ptr<http_response>)> handler) {
        // save request and initiate connection or handle output queue
        socket_->get_io_service().post([this, request=std::move(request), handler=std::move(handler), self = shared_from_this()] {
            size_t queue_size = request_queue_.size();
            if (queue_size >= MAX_OUTPUT_REQUESTS) {
                LOG_WARNING("discarding http request, output queue is complete (%d)", MAX_OUTPUT_REQUESTS);
                handler(boost::asio::error::operation_aborted, std::shared_ptr<http_response>());
                return;
            }
            bool wasEmpty = queue_size == 0;

            // push to the request queue
            request_queue_.emplace(request, handler);

            // only handle output if queue is empty. Otherwise, the queue will be processed as other request are ending
            if (wasEmpty) {
                if (!socket_->is_open()) {
                    LOG_WARNING("connection is closed... initiating a new connection");
                    handle_socket_connection();
                } else {
                    LOG_WARNING("reusing previous socket connection");
                    // write our request or handle the socket connection
                    handle_output_queue();
                }
            }
        });
    }

    std::shared_ptr<thinger::asio::socket> http_client_connection::release_socket() {
        socket_->cancel();
        shared_keeper_->clear();
        return socket_;
    }

}