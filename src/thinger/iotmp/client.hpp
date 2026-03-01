// Async client - uses same include guard as client.hpp so extensions work
#ifndef THINGER_IOTMP_CLIENT_HPP
#define THINGER_IOTMP_CLIENT_HPP

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/thread_pool.hpp>

#include <string>
#include <chrono>
#include <map>
#include <queue>
#include <future>
#include <optional>

#include "core/iotmp_types.hpp"
#include "core/iotmp_message.hpp"
#include "core/iotmp_encoder.hpp"
#include "core/iotmp_decoder.hpp"
#include "core/iotmp_resource.hpp"
#include "core/iotmp_server_event.hpp"
#include "core/iotmp_logger.hpp"

// Use thinger-http sockets and client (modern coroutine-based API)
#include <thinger/asio/sockets/socket.hpp>
#include <thinger/asio/sockets/tcp_socket.hpp>
#include <thinger/asio/sockets/ssl_socket.hpp>
#include <thinger/asio/sockets/websocket.hpp>
#include <thinger/http/client/async_client.hpp>
#include <thinger/asio/workers.hpp>
#include <thinger/asio/worker_client.hpp>
#include <thinger/util/types.hpp>
#include <thinger/util/logger.hpp>

namespace thinger::iotmp {

    namespace asio = boost::asio;
    using thinger::awaitable;
    using thinger::use_awaitable;
    using thinger::use_nothrow_awaitable;
    using thinger::co_spawn;
    using thinger::detached;

    // Client state for callbacks
    enum class client_state {
        CONNECTING,
        CONNECTED,
        AUTHENTICATING,
        AUTHENTICATED,
        AUTH_FAILED,
        DISCONNECTED,
        CONNECTION_ERROR,
        STREAMS_READY
    };

    // Transport type for socket selection
    enum class transport_type {
        TCP,
        SSL,
        WEBSOCKET
    };

    // Stream configuration
    struct stream_config {
        iotmp_resource* resource = nullptr;
        unsigned int interval = 0;
        unsigned long last_streaming = 0;
    };

    // Async IOTMP client using C++20 coroutines
    class client : public thinger::asio::worker_client {
    public:
        static constexpr size_t MAX_MESSAGE_SIZE = 256 * 1024;  // 256KB max (chunks + protocol overhead)
        static constexpr auto KEEP_ALIVE_INTERVAL = std::chrono::seconds(60);
        static constexpr auto CONNECT_TIMEOUT = std::chrono::seconds(15);
        static constexpr auto RECONNECT_DELAY = std::chrono::seconds(5);

        client() : worker_client("iotmp") {}

        // State callback
        void set_state_callback(std::function<void(client_state, const std::string&)> callback) {
            state_callback_ = std::move(callback);
        }

        void set_credentials(std::string user, std::string device, std::string password) {
            username_ = std::move(user);
            device_id_ = std::move(device);
            device_password_ = std::move(password);
        }

        void set_host(std::string host) {
            host_ = std::move(host);
        }

        void set_transport(transport_type transport) {
            transport_ = transport;
            // Set default port based on transport
            switch(transport) {
                case transport_type::SSL:
                    port_ = 25206;
                    break;
                case transport_type::TCP:
                    port_ = 25204;
                    break;
                case transport_type::WEBSOCKET:
                    port_ = 443;
                    break;
            }
        }

        // Getters
        const std::string& get_user() const { return username_; }
        const std::string& get_device() const { return device_id_; }
        const std::string& get_credentials() const { return device_password_; }
        const std::string& get_host() const { return host_; }
        uint16_t get_port() const { return port_; }
        bool is_secure() const { return socket_ ? socket_->is_secure() : false; }

        // Resource access
        iotmp_resource& operator[](std::string_view path) {
            std::string path_str(path);
            return resources_[path_str](path_str);
        }

        // Start the client
        bool start() override {
            if(!worker_client::start()) return false;
            // Use thinger-http worker pool
            auto& io = thinger::asio::get_workers().get_next_io_context();
            co_spawn(io, run_loop(), detached);
            return true;
        }

        // Stop the client
        bool stop() override {
            if(!worker_client::stop()) return false;
            if(keep_alive_timer_) keep_alive_timer_->cancel();
            if(stream_timer_) stream_timer_->cancel();
            resource_pool_.join();
            if(socket_) {
                socket_->close();
                socket_.reset();
            }
            notify_state(client_state::DISCONNECTED);
            return true;
        }

        // Implements client
        bool is_connected() const {
            return connected_ && socket_ && socket_->is_open();
        }

        // Get io_context from socket (available after connect)
        asio::io_context& get_io_context() {
            return socket_->get_io_context();
        }

        // Stop a stream
        bool stop_stream(uint16_t stream_id) {
            if(!connected_) return false;
            iotmp_message msg(message::type::STOP_STREAM);
            msg[message::field::STREAM_ID] = stream_id;
            send_message(msg);
            return true;
        }

        // Stream binary data
        bool stream_resource(uint16_t stream_id, const uint8_t* data, size_t size) {
            if(!connected_) return false;
            iotmp_message msg(message::type::STREAM_DATA);
            msg[message::field::STREAM_ID] = stream_id;
            msg[message::field::PAYLOAD] = json_t::binary({data, data + size});
            send_message(msg);
            return true;
        }

        // Stream JSON data
        bool stream_resource(uint16_t stream_id, json_t&& data) {
            if(!connected_) return false;
            iotmp_message msg(message::type::STREAM_DATA);
            msg[message::field::STREAM_ID] = stream_id;
            msg[message::field::PAYLOAD].swap(data);
            send_message(msg);
            return true;
        }

        // ============== Request-Response API (coroutines) ==============

        // Wait for response with specific stream_id, processing other messages meanwhile
        awaitable<bool> wait_response(iotmp_message& request, json_t* payload = nullptr) {
            uint16_t request_stream_id = request.get_stream_id();
            while(true) {
                auto response = co_await read_message();
                if(!response) co_return false;

                uint16_t response_stream_id = response->get_stream_id();

                if(request_stream_id == response_stream_id &&
                   response->get_message_type() <= message::type::ERROR) {
                    if(payload && response->has_field(message::field::PAYLOAD)) {
                        payload->swap((*response)[message::field::PAYLOAD]);
                    }
                    co_return response->get_message_type() == message::type::OK;
                } else {
                    co_await handle_message(std::move(*response));
                }
            }
        }

        // Send message and wait for acknowledgement
        awaitable<bool> send_message_with_ack(iotmp_message& message, bool wait_ack = true) {
            if(wait_ack && message.get_stream_id() == 0) {
                message.set_random_stream_id();
            }
            if(!co_await write_message(message)) co_return false;
            if(!wait_ack) co_return true;
            co_return co_await wait_response(message);
        }

        // Send message and wait for response payload
        awaitable<bool> send_message_with_response(iotmp_message& message, json_t& response_payload) {
            if(message.get_stream_id() == 0) {
                message.set_random_stream_id();
            }
            if(!co_await write_message(message)) co_return false;
            co_return co_await wait_response(message, &response_payload);
        }

        // ============== Server API (coroutines) ==============

        // Read a property from the server
        awaitable<bool> get_property(std::string_view property_id, json_t& data) {
            iotmp_message msg(message::type::RUN);
            msg[message::field::RESOURCE] = static_cast<uint32_t>(server::run::READ_DEVICE_PROPERTY);
            msg[message::field::PARAMETERS] = std::string(property_id);
            co_return co_await send_message_with_response(msg, data);
        }

        // Write a property to the server
        awaitable<bool> set_property(std::string_view property_id, json_t data, bool confirm = false) {
            iotmp_message msg(message::type::RUN);
            msg[message::field::RESOURCE] = static_cast<uint32_t>(server::run::SET_DEVICE_PROPERTY);
            msg[message::field::PARAMETERS] = std::string(property_id);
            msg[message::field::PAYLOAD].swap(data);
            co_return co_await send_message_with_ack(msg, confirm);
        }

        // Write data to a bucket
        awaitable<bool> write_bucket(std::string_view bucket_id, json_t data, bool confirm = false) {
            iotmp_message msg(message::type::RUN);
            msg[message::field::RESOURCE] = static_cast<uint32_t>(server::run::WRITE_BUCKET);
            msg[message::field::PARAMETERS] = std::string(bucket_id);
            msg[message::field::PAYLOAD].swap(data);
            co_return co_await send_message_with_ack(msg, confirm);
        }

        // Call endpoint without data
        awaitable<bool> call_endpoint(std::string_view endpoint_name, bool confirm = false) {
            iotmp_message msg(message::type::RUN);
            msg[message::field::RESOURCE] = static_cast<uint32_t>(server::run::CALL_ENDPOINT);
            msg[message::field::PARAMETERS] = std::string(endpoint_name);
            co_return co_await send_message_with_ack(msg, confirm);
        }

        // Call endpoint with data
        awaitable<bool> call_endpoint(std::string_view endpoint_name, json_t data, bool confirm = false) {
            iotmp_message msg(message::type::RUN);
            msg[message::field::RESOURCE] = static_cast<uint32_t>(server::run::CALL_ENDPOINT);
            msg[message::field::PARAMETERS] = std::string(endpoint_name);
            msg[message::field::PAYLOAD].swap(data);
            co_return co_await send_message_with_ack(msg, confirm);
        }

        // Call device resource without data
        awaitable<bool> call_device(std::string_view device, std::string_view resource, bool confirm = false) {
            iotmp_message msg(message::type::RUN);
            msg[message::field::RESOURCE] = static_cast<uint32_t>(server::run::CALL_DEVICE);
            msg[message::field::PARAMETERS] = json_t::array({std::string(device), std::string(resource)});
            co_return co_await send_message_with_ack(msg, confirm);
        }

        // Call device resource with data
        awaitable<bool> call_device(std::string_view device, std::string_view resource, json_t data, bool confirm = false) {
            iotmp_message msg(message::type::RUN);
            msg[message::field::RESOURCE] = static_cast<uint32_t>(server::run::CALL_DEVICE);
            msg[message::field::PARAMETERS] = json_t::array({std::string(device), std::string(resource)});
            msg[message::field::PAYLOAD].swap(data);
            co_return co_await send_message_with_ack(msg, confirm);
        }

        // Lock sync primitive
        awaitable<bool> lock_sync(std::string_view sync_id, std::string_view lock_id = "",
                                        uint16_t slots = 1, uint16_t timeout_seconds = 0) {
            iotmp_message msg(message::type::RUN);
            msg[message::field::RESOURCE] = static_cast<uint32_t>(server::run::LOCK_SYNC);
            msg[message::field::PARAMETERS] = std::string(sync_id);
            if(!lock_id.empty()) msg[message::field::PAYLOAD]["lock"] = std::string(lock_id);
            if(slots > 1) msg[message::field::PAYLOAD]["slots"] = slots;
            if(timeout_seconds > 0) msg[message::field::PAYLOAD]["timeout"] = timeout_seconds;
            co_return co_await send_message_with_ack(msg);
        }

        // Unlock sync primitive
        awaitable<bool> unlock_sync(std::string_view sync_id, std::string_view lock_id) {
            iotmp_message msg(message::type::RUN);
            msg[message::field::RESOURCE] = static_cast<uint32_t>(server::run::UNLOCK_SYNC);
            msg[message::field::PARAMETERS] = std::string(sync_id);
            msg[message::field::PAYLOAD]["lock"] = std::string(lock_id);
            co_return co_await send_message_with_ack(msg);
        }

        // ============== Server Event Subscriptions ==============

        // Subscribe to publish on MQTT topic
        iotmp_server_event& topic_publish_stream(std::string_view topic, uint8_t qos = 0, bool retained = false) {
            uint8_t event_id = events_.empty() ? 1 : events_.rbegin()->first + 1;
            auto& event = events_[event_id];
            event.set_resource(server::run::SUBSCRIBE_EVENT);
            auto& params = event.get_params();
            params["event"] = "device_topic_publish";
            params["scope"] = "publish";
            params["topic"] = std::string(topic);
            if(qos) params["qos"] = qos;
            if(retained) params["retained"] = retained;
            return event;
        }

        // Subscribe to receive from MQTT topic
        iotmp_server_event& topic_subscribe_stream(std::string_view topic, uint8_t qos = 0) {
            uint8_t event_id = events_.empty() ? 1 : events_.rbegin()->first + 1;
            auto& event = events_[event_id];
            event.set_resource(server::run::SUBSCRIBE_EVENT);
            auto& params = event.get_params();
            params["event"] = "device_topic_publish";
            params["scope"] = "subscribe";
            params["topic"] = std::string(topic);
            if(qos) params["qos"] = qos;
            return event;
        }

        // Subscribe to property changes
        iotmp_server_event& property_stream(std::string_view property, bool fetch_at_subscription = false) {
            uint8_t event_id = events_.empty() ? 1 : events_.rbegin()->first + 1;
            auto& event = events_[event_id];
            event.set_resource(server::run::SUBSCRIBE_EVENT);
            auto& params = event.get_params();
            params["event"] = "device_property_update";
            if(fetch_at_subscription) params["current"] = true;
            params["filters"]["property"] = std::string(property);
            return event;
        }

        // Subscribe to generic server event
        iotmp_server_event& event_subscribe(std::string_view event_name) {
            uint8_t event_id = events_.empty() ? 1 : events_.rbegin()->first + 1;
            auto& event = events_[event_id];
            event.set_resource(server::run::SUBSCRIBE_EVENT);
            auto& params = event.get_params();
            params["event"] = std::string(event_name);
            return event;
        }

    private:
        // Main coroutine - handles connection, reconnection, reading
        awaitable<void> run_loop() {
            while(running_) {
                try {
                    auto ec = co_await connect();
                    if(ec) {
                        notify_state(client_state::CONNECTION_ERROR, ec.message());
                        LOG_ERROR("Connection error: {}", ec.message());
                    } else if(!co_await authenticate()) {
                        LOG_ERROR("Authentication failed");
                    } else {
                        LOG_INFO("Authenticated successfully!");
                        connected_ = true;

                        // Initialize server event streams (MQTT topics, properties, etc.)
                        co_await initialize_streams();

                        // Launch keep-alive in parallel (use socket's io_context)
                        co_spawn(get_io_context(), keep_alive_loop(), detached);

                        // Launch stream interval loop for periodic streaming
                        co_spawn(get_io_context(), stream_interval_loop(), detached);

                        notify_state(client_state::STREAMS_READY);

                        // Message read loop
                        co_await read_loop();
                    }
                } catch(const std::exception& e) {
                    LOG_ERROR("Exception in connection cycle: {}", e.what());
                    notify_state(client_state::CONNECTION_ERROR, e.what());
                } catch(...) {
                    LOG_ERROR("Unknown exception in connection cycle");
                    notify_state(client_state::CONNECTION_ERROR, "unknown exception");
                }

                connected_ = false;
                notify_state(client_state::DISCONNECTED);
                if(keep_alive_timer_) keep_alive_timer_->cancel();
                if(stream_timer_) stream_timer_->cancel();

                if(running_) {
                    LOG_INFO("Reconnecting in {} seconds...", RECONNECT_DELAY.count());
                    co_await delay(RECONNECT_DELAY);
                }
            }
        }

        // Connect to server
        awaitable<boost::system::error_code> connect() {
            LOG_INFO("Connecting to {}:{}...", host_, port_);
            notify_state(client_state::CONNECTING);

            if(transport_ == transport_type::WEBSOCKET) {
                // WebSocket needs special handling: SSL connect + HTTP upgrade
                auto ec = co_await websocket_connect();
                if(ec) co_return ec;
            } else {
                // Create socket based on transport type (runtime dispatch)
                socket_ = create_socket();
                // Connect (thinger-http socket handles handshake internally if needed)
                auto ec = co_await socket_->connect(host_, std::to_string(port_), CONNECT_TIMEOUT);
                if(ec) co_return ec;
            }

            // Initialize timers using socket's io_context (ensures same thread)
            auto& io = socket_->get_io_context();
            keep_alive_timer_.emplace(io);
            stream_timer_.emplace(io);

            notify_state(client_state::CONNECTED);
            LOG_INFO("Connected!");
            co_return boost::system::error_code{};
        }

        // WebSocket connection using thinger-http pool_client (async)
        awaitable<boost::system::error_code> websocket_connect() {
            // Build WebSocket URL
            std::string ws_url = "wss://" + host_ + ":" + std::to_string(port_) + "/iotmp";

            // Use async client for WebSocket upgrade
            thinger::http::async_client http_client;
            auto ws_client = co_await http_client.request(ws_url).protocol("iotmp").websocket();

            if(!ws_client) {
                co_return boost::asio::error::connection_refused;
            }

            // Release the underlying websocket for direct use
            socket_ = ws_client->release_socket();

            LOG_DEBUG("WebSocket upgrade successful");
            co_return boost::system::error_code{};
        }

        // Socket factory - runtime dispatch based on transport type
        std::shared_ptr<thinger::asio::socket> create_socket() {
            // Use pool workers io_context for socket operations
            auto& io = thinger::asio::get_workers().get_thread_io_context();

            switch(transport_) {
                case transport_type::SSL: {
                    if(!ssl_context_) {
                        ssl_context_ = std::make_shared<boost::asio::ssl::context>(
                            boost::asio::ssl::context::tlsv12_client);
                        ssl_context_->set_verify_mode(boost::asio::ssl::verify_none);
                    }
                    return std::make_shared<thinger::asio::ssl_socket>("iotmp_client", io, ssl_context_);
                }
                case transport_type::TCP:
                    return std::make_shared<thinger::asio::tcp_socket>("iotmp_client", io);
                case transport_type::WEBSOCKET:
                    // WebSocket is handled by websocket_connect() using http::client
                    return nullptr;
            }
            return nullptr;
        }

        // Authenticate with server
        awaitable<bool> authenticate() {
            LOG_INFO("Authenticating as {}@{}...", device_id_, username_);
            notify_state(client_state::AUTHENTICATING);

            iotmp_message connect_msg(message::type::CONNECT);
            connect_msg.set_random_stream_id();
            connect_msg[message::field::PAYLOAD] = json_t::array({username_, device_id_, device_password_});

            if(!co_await write_message(connect_msg)) {
                notify_state(client_state::AUTH_FAILED);
                co_return false;
            }
            auto response = co_await read_message();
            if(!response) {
                notify_state(client_state::AUTH_FAILED);
                co_return false;
            }

            bool success = response->get_message_type() == message::type::OK;
            notify_state(success ? client_state::AUTHENTICATED : client_state::AUTH_FAILED);
            co_return success;
        }

        // Initialize server event streams (MQTT topics, properties, etc.)
        awaitable<void> initialize_streams() {
            static json_t dummy;

            for(auto& [event_id, event] : events_) {
                iotmp_message request(message::type::START_STREAM);
                request.set_random_stream_id();
                request[message::field::RESOURCE] = static_cast<uint32_t>(event.get_resource());
                request[message::field::PARAMETERS].swap(event.get_params());

                // Send start stream request
                if(!co_await write_message(request)) continue;

                // Wait for response
                json_t response_data;
                bool success = co_await wait_response(request, &response_data);

                if(success) {
                    // Register stream
                    uint16_t stream_id = request.get_stream_id();
                    auto& stream_cfg = streams_[stream_id];
                    stream_cfg.resource = &event;
                    stream_cfg.interval = 0;
                    event.set_stream_id(stream_id);

                    // If response has data, run the event handler with it
                    if(!response_data.is_null() && !response_data.empty()) {
                        iotmp_message req(message::type::RUN);
                        req[message::field::PAYLOAD].swap(response_data);
                        iotmp_message resp(message::type::OK);
                        event.run_resource(req, resp);
                    }
                }

                // Restore params
                event.get_params().swap(request[message::field::PARAMETERS]);
            }
        }

        // Message read loop
        awaitable<void> read_loop() {
            while(running_ && connected_) {
                auto message = co_await read_message();
                if(!message) break;
                co_spawn(get_io_context(), handle_message(std::move(*message)), detached);
            }
        }

        // Read a complete message (returns nullopt on connection error)
        awaitable<std::optional<iotmp_message>> read_message() {
            // Read header: type (1 byte) + size (varint)
            uint8_t type_byte;
            {
                auto [ec, n] = co_await socket_->read(&type_byte, 1);
                if(ec) co_return std::nullopt;
            }

            auto size = co_await read_varint();
            if(!size) co_return std::nullopt;

            if(*size > MAX_MESSAGE_SIZE) {
                LOG_ERROR("Message too large: {} bytes", *size);
                co_return std::nullopt;
            }

            iotmp_message message(static_cast<message::type>(type_byte));

            if(*size > 0) {
                read_buffer_.resize(*size);
                auto [ec, n] = co_await socket_->read(read_buffer_.data(), *size);
                if(ec) co_return std::nullopt;

                memory_reader reader(read_buffer_.data(), *size);
                iotmp_decoder<memory_reader> decoder(reader);
                decoder.decode(message, *size);
            }

            if(message.get_message_type() != message::STREAM_DATA) {
                message_logger::log_incoming(message);
            }

            co_return message;
        }

        // Read varint from socket (returns nullopt on error)
        awaitable<std::optional<uint32_t>> read_varint() {
            uint32_t value = 0;
            uint8_t bit_pos = 0;
            uint8_t byte;

            do {
                auto [ec, n] = co_await socket_->read(&byte, 1);
                if(ec) co_return std::nullopt;
                value |= static_cast<uint32_t>(byte & 0x7F) << bit_pos;
                bit_pos += 7;
                if(bit_pos >= 32) {
                    LOG_ERROR("Varint too large");
                    co_return std::nullopt;
                }
            } while(byte & 0x80);

            co_return value;
        }

        // Send message (fire-and-forget using co_spawn)
        void send_message(iotmp_message& message) {
            if(message.get_message_type() != message::STREAM_DATA) {
                message_logger::log_outgoing(message);
            }

            write_queue_.emplace(encode_message(message));

            if(!write_in_progress_) {
                write_in_progress_ = true;
                co_spawn(get_io_context(), process_write_queue(), detached);
            }
        }

        // Process write queue (coroutine-based)
        awaitable<void> process_write_queue() {
            while(!write_queue_.empty() && connected_ && socket_) {
                auto data = std::move(write_queue_.front());
                write_queue_.pop();

                auto [ec, bytes] = co_await socket_->write(reinterpret_cast<const uint8_t*>(data.data()), data.size());
                if(ec) {
                    LOG_ERROR("Write error: {}", ec.message());
                    break;
                }
            }
            write_in_progress_ = false;
        }

        // Write message and wait for completion (coroutine)
        awaitable<bool> write_message(iotmp_message& message) {
            if(message.get_message_type() != message::STREAM_DATA) {
                message_logger::log_outgoing(message);
            }

            auto encoded = encode_message(message);
            auto [ec, bytes] = co_await socket_->write(reinterpret_cast<const uint8_t*>(encoded.data()), encoded.size());
            if(ec) {
                LOG_ERROR("Write error: {}", ec.message());
                co_return false;
            }
            co_return true;
        }

        // Keep-alive loop
        awaitable<void> keep_alive_loop() {
            while(running_ && connected_ && keep_alive_timer_) {
                keep_alive_timer_->expires_after(KEEP_ALIVE_INTERVAL);

                auto [ec] = co_await keep_alive_timer_->async_wait(use_nothrow_awaitable);

                if(ec) break;

                if(connected_) {
                    iotmp_message ka(message::type::KEEP_ALIVE);
                    send_message(ka);
                    LOG_DEBUG("Keep-alive sent");
                }
            }
        }

        // Stream interval loop - handles periodic streaming of resources
        awaitable<void> stream_interval_loop() {
            static constexpr auto STREAM_CHECK_INTERVAL = std::chrono::seconds(1);

            while(running_ && connected_ && stream_timer_) {
                stream_timer_->expires_after(STREAM_CHECK_INTERVAL);

                auto [ec] = co_await stream_timer_->async_wait(use_nothrow_awaitable);

                if(ec) break;

                if(!connected_) break;

                auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();

                // Check each stream for interval-based streaming
                for(auto& [stream_id, config] : streams_) {
                    if(config.interval > 0 && config.resource) {
                        auto elapsed = now - config.last_streaming;
                        if(elapsed >= config.interval) {
                            config.last_streaming = now;
                            stream_resource(*config.resource, stream_id);
                        }
                    }
                }
            }
        }

        // Delay helper
        awaitable<void> delay(std::chrono::seconds duration) {
            try {
                // Use current thread's io_context (works even without socket)
                auto& io = thinger::asio::get_workers().get_thread_io_context();
                asio::steady_timer timer(io, duration);
                auto [ec] = co_await timer.async_wait(use_nothrow_awaitable);
            } catch(const std::exception& e) {
                LOG_ERROR("Exception in delay timer: {}", e.what());
            }
        }

        // Handle received message (coroutine - may dispatch blocking work to thread pool)
        awaitable<void> handle_message(iotmp_message message) {
            switch(message.get_message_type()) {
                case message::KEEP_ALIVE:
                    LOG_DEBUG("Keep-alive received");
                    break;

                case message::RUN:
                case message::DESCRIBE:
                case message::START_STREAM:
                case message::STOP_STREAM:
                case message::STREAM_DATA:
                    co_await handle_resource_request(message);
                    break;

                default:
                    LOG_WARNING("Unhandled message type: {}",
                        static_cast<int>(message.get_message_type()));
                    break;
            }
        }

        // Match resource path with wildcards
        bool matches(std::string_view res_path, std::string_view req_path, json_t& path_matches) {
            size_t res_idx = 0;
            size_t req_idx = 0;

            while(res_idx < res_path.size()) {
                if(res_path[res_idx] == '*') {
                    ++res_idx;
                    size_t key_start = res_idx;
                    while(res_idx < res_path.size() && res_path[res_idx] != '/') ++res_idx;

                    if(key_start == res_idx) return false;

                    std::string key(res_path.substr(key_start, res_idx - key_start));
                    path_matches[key] = std::string(req_path.substr(req_idx));
                    return res_idx == res_path.size();
                }
                else if(res_path[res_idx] == ':') {
                    ++res_idx;
                    size_t key_start = res_idx;
                    while(res_idx < res_path.size() && res_path[res_idx] != '/') ++res_idx;
                    std::string key(res_path.substr(key_start, res_idx - key_start));

                    size_t value_start = req_idx;
                    while(req_idx < req_path.size() && req_path[req_idx] != '/') ++req_idx;
                    std::string value(req_path.substr(value_start, req_idx - value_start));

                    path_matches[key] = value;
                }
                else if(req_idx >= req_path.size() || res_path[res_idx] != req_path[req_idx]) {
                    return false;
                }
                else {
                    ++res_idx;
                    ++req_idx;
                }
            }
            return res_idx == res_path.size() && req_idx == req_path.size();
        }

        // Find resource by path
        iotmp_resource* get_resource(const std::string& request_path, json_t& path_matches) {
            for(auto& [resource_path, resource] : resources_) {
                if(matches(resource_path, request_path, path_matches)) {
                    return &resource;
                }
            }
            return nullptr;
        }

        // Handle resource request (coroutine - RUN dispatches to thread pool)
        awaitable<void> handle_resource_request(iotmp_message& request) {
            iotmp_resource* resource = nullptr;

            auto msg_type = request.get_message_type();
            if(msg_type == message::STREAM_DATA || msg_type == message::STOP_STREAM) {
                auto it = streams_.find(request.get_stream_id());
                if(it != streams_.end()) {
                    resource = it->second.resource;
                }
            }

            if(!resource && request.has_field(message::field::RESOURCE)) {
                const auto& res = request[message::field::RESOURCE];
                if(res.is_string()) {
                    resource = get_resource(res.get<std::string>(), request[0]);
                }
            }

            if(!resource) {
                // Handle DESCRIBE without specific resource (API listing)
                if(msg_type == message::DESCRIBE && !request.has_field(message::field::RESOURCE)) {
                    iotmp_message response(request.get_stream_id(), message::type::OK);
                    for(auto& [resource_path, res] : resources_) {
                        res.fill_api(response[message::field::PAYLOAD][resource_path]);
                    }
                    send_message(response);
                    co_return;
                }

                if(msg_type != message::STREAM_DATA) {
                    iotmp_message error(request.get_stream_id(), message::type::ERROR);
                    send_message(error);
                }
                co_return;
            }

            switch(request.get_message_type()) {
                case message::RUN: {
                    iotmp_message response(request.get_stream_id(), message::type::OK);
                    // Dispatch blocking resource execution to thread pool
                    // After co_await, execution resumes on io_context (safe for send_message)
                    bool success = co_await co_spawn(resource_pool_.get_executor(),
                        [resource, &request, &response]() -> awaitable<bool> {
                            co_return resource->run_resource(request, response);
                        }(), use_awaitable);
                    response.set_message_type(success ? message::type::OK : message::type::ERROR);
                    send_message(response);
                    break;
                }

                case message::DESCRIBE: {
                    iotmp_message response(request.get_stream_id(), message::type::OK);
                    resource->describe(response);
                    send_message(response);
                    break;
                }

                case message::START_STREAM: {
                    uint16_t stream_id = request.get_stream_id();
                    auto& stream_cfg = streams_[stream_id];
                    stream_cfg.resource = resource;
                    stream_cfg.interval = get_value(request.params(), "interval", 0u);
                    if(stream_cfg.interval == 0) {
                        resource->set_stream_id(stream_id);
                    }

                    if(resource->has_stream_handler()) {
                        resource->handle_stream(stream_id, request[0], request.params(), true,
                            [this, stream_id, resource](exec_result&& result) {
                                auto& msg = result.get_message();
                                msg.set_stream_id(stream_id);
                                send_message(msg);
                                if(result && resource->stream_echo()) {
                                    stream_resource(*resource, stream_id);
                                }
                            });
                    } else {
                        iotmp_message response(stream_id, message::type::OK);
                        send_message(response);
                        if(resource->stream_echo()) {
                            stream_resource(*resource, stream_id);
                        }
                    }
                    break;
                }

                case message::STOP_STREAM: {
                    uint16_t stream_id = request.get_stream_id();
                    if(resource->get_stream_id() == stream_id) {
                        resource->set_stream_id(0);
                    }
                    streams_.erase(stream_id);

                    if(resource->has_stream_handler()) {
                        json_t empty_params;
                        resource->handle_stream(stream_id, request.params(), empty_params, false,
                            [this, stream_id](exec_result&& result) {
                                auto& msg = result.get_message();
                                msg.set_stream_id(stream_id);
                                send_message(msg);
                            });
                    } else {
                        iotmp_message response(stream_id, message::type::OK);
                        send_message(response);
                    }
                    break;
                }

                case message::STREAM_DATA: {
                    iotmp_message response(request.get_stream_id(), message::type::STREAM_DATA);
                    resource->run_resource(request, response);
                    // after receiving input on a streamed resource, echo back current state
                    if(resource->stream_echo() &&
                       (resource->get_io_type() == iotmp_resource::input_wrapper ||
                        resource->get_io_type() == iotmp_resource::input_output_wrapper)) {
                        stream_resource(*resource, request.get_stream_id());
                    }
                    break;
                }

                default:
                    break;
            }
        }

        // Stream resource data
        bool stream_resource(iotmp_resource& resource, uint16_t stream_id) {
            iotmp_message request(message::type::STREAM_DATA), response(message::type::STREAM_DATA);
            resource.run_resource(request, response);
            // output resources write to response, input resources write to request
            auto& msg = response.has_field(message::field::PAYLOAD) ? response : request;
            if(msg.has_field(message::field::PAYLOAD)) {
                msg.set_stream_id(stream_id);
                send_message(msg);
                return true;
            }
            return false;
        }

        // Notify state change
        void notify_state(client_state state, const std::string& reason = "") {
            if(state_callback_) {
                state_callback_(state, reason);
            }
        }

        // Execute callback in io_context (synchronized)
    public:
        bool run(std::function<bool()> callback) {
            if(!socket_) return false;
            std::promise<bool> p;
            asio::dispatch(get_io_context(), [this, &p, callback = std::move(callback)]() {
                if(!connected_) {
                    p.set_value(false);
                    return;
                }
                p.set_value(callback());
            });
            return p.get_future().get();
        }

    private:
        transport_type transport_ = transport_type::SSL;
        std::shared_ptr<thinger::asio::socket> socket_;
        std::shared_ptr<boost::asio::ssl::context> ssl_context_;
        std::optional<asio::steady_timer> keep_alive_timer_;
        std::optional<asio::steady_timer> stream_timer_;

        std::string host_;
        uint16_t port_ = 25206;
        std::string username_;
        std::string device_id_;
        std::string device_password_;

        std::map<std::string, iotmp_resource> resources_;
        std::map<uint16_t, stream_config> streams_;
        std::map<uint8_t, iotmp_server_event> events_;
        std::vector<uint8_t> read_buffer_;

        // Thread pool for dispatching blocking resource executions (e.g., scripts)
        asio::thread_pool resource_pool_{std::min(std::thread::hardware_concurrency(), 4u)};

        // Write queue for serialized writes
        std::queue<std::string> write_queue_;
        bool write_in_progress_ = false;

        bool connected_ = false;

        // State callback
        std::function<void(client_state, const std::string&)> state_callback_;
    };

}

#endif
