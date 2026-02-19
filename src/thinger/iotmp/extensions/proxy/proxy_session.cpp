#include "proxy_session.hpp"
#include <thinger/asio/sockets/socket.hpp>
#include <thinger/asio/sockets/tcp_socket.hpp>
#include <thinger/asio/sockets/ssl_socket.hpp>

namespace thinger::iotmp {

proxy_session::proxy_session(client& client, uint16_t stream_id, std::string session,
                             std::string host, uint16_t port, bool secure)
    : stream_session(client, stream_id, std::move(session)),
      host_(std::move(host)),
      port_(port)
{
    // Create socket based on security setting
    auto& io = client.get_io_context();
    if(secure) {
        auto ssl_context = std::make_shared<boost::asio::ssl::context>(
            boost::asio::ssl::context::sslv23_client);
        ssl_context->set_default_verify_paths();
        socket_ = std::make_shared<thinger::asio::ssl_socket>("proxy", io, ssl_context);
    } else {
        socket_ = std::make_shared<thinger::asio::tcp_socket>("proxy", io);
    }
}

proxy_session::~proxy_session() {
    THINGER_LOG("[{}] releasing tcp proxy session", stream_id_);
}

awaitable<exec_result> proxy_session::start() {
    THINGER_LOG("[{}] starting proxy session: {}:{}", stream_id_, host_, port_);

    try {
        co_await socket_->connect(host_, std::to_string(port_), std::chrono::seconds(10));
        THINGER_LOG("[{}] target proxy connected: {}:{}", stream_id_, host_, port_);

        running_ = true;

        // Launch read loop
        co_spawn(socket_->get_io_context(), read_loop(), detached);

        co_return true;

    } catch(const std::exception& e) {
        THINGER_LOG_ERROR("[{}] error on proxy connection: {}:{} ({})",
                         stream_id_, host_, port_, e.what());
        co_return exec_result{false, e.what()};
    }
}

bool proxy_session::stop(StopReason reason) {
    running_ = false;
    if(socket_ && socket_->is_open()) {
        socket_->close();
    }
    return true;
}

void proxy_session::handle_input(input& in) {
    if(!running_) return;

    // Proxy expects binary data
    if(!in->is_binary()) return;

    auto& binary = in->get_binary();
    if(binary.empty()) return;

    // Queue data for writing
    write_queue_.emplace(reinterpret_cast<const char*>(binary.data()), binary.size());

    // Start write loop if not already running
    if(!write_in_progress_) {
        write_in_progress_ = true;
        co_spawn(socket_->get_io_context(), write_loop(), detached);
    }
}

awaitable<void> proxy_session::read_loop() {
    auto self = shared_from_this();

    while(running_ && socket_->is_open()) {
        try {
            auto bytes = co_await socket_->read_some(read_buffer_, PROXY_BUFFER_SIZE);

            if(bytes > 0) {
                increase_received(bytes);

                if(!client_.stream_resource(stream_id_, read_buffer_, bytes)) {
                    THINGER_LOG_ERROR("[{}] cannot send proxy data for stream id", stream_id_);
                    stop();
                    break;
                }
            }

        } catch(const boost::system::system_error& e) {
            if(e.code() != boost::asio::error::operation_aborted) {
                THINGER_LOG_ERROR("[{}] error reading from proxy after {}: {}",
                                 stream_id_, last_usage().count(), e.what());
                stop();
            }
            break;
        } catch(const std::exception& e) {
            THINGER_LOG_ERROR("[{}] unexpected error in proxy read_loop: {}",
                             stream_id_, e.what());
            stop();
            break;
        }
    }
}

awaitable<void> proxy_session::write_loop() {
    auto self = shared_from_this();

    while(running_ && !write_queue_.empty() && socket_->is_open()) {
        auto data = std::move(write_queue_.front());
        write_queue_.pop();

        try {
            co_await socket_->write(reinterpret_cast<const uint8_t*>(data.data()), data.size());
            increase_sent(data.size());

        } catch(const boost::system::system_error& e) {
            if(e.code() != boost::asio::error::operation_aborted) {
                THINGER_LOG_ERROR("[{}] error writing to proxy: {} ({})",
                                 stream_id_, e.code().value(), e.what());
                stop();
            }
            break;
        } catch(const std::exception& e) {
            THINGER_LOG_ERROR("[{}] unexpected error in proxy write_loop: {}",
                             stream_id_, e.what());
            stop();
            break;
        }
    }

    write_in_progress_ = false;
}

}
