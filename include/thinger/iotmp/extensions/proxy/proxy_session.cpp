#include "proxy_session.hpp"
#include "../../../asio/sockets/socket.hpp"

namespace thinger::iotmp{

    proxy_session::proxy_session(client& client, uint16_t stream_id, std::string session, std::string host,
                                 uint16_t port)
            : stream_session(client, stream_id, std::move(session)),
              socket_("proxy", client.get_io_service()),
              host_(std::move(host)),
              port_(port)
    {
        //THINGER_LOG("[%u] created proxy session: %s:%u", stream_id_, host_.c_str(), port_);

    }

    proxy_session::~proxy_session(){
        THINGER_LOG("[%u] releasing tcp proxy session", stream_id_);
    }

    void proxy_session::start(result_handler handler){
        THINGER_LOG("[%u] starting proxy session: %s:%u", stream_id_, host_.c_str(), port_);

        socket_.connect(host_,
                        std::to_string(port_),
                        std::chrono::seconds(10),
                        [this, shelf = shared_from_this(), handler = std::move(handler)](const boost::system::error_code & ec){
                            if(!ec){
                                THINGER_LOG("[%u] target proxy connected: %s:%u", stream_id_, host_.c_str(), port_);
                                handler(true);
                                // handle write (if any data is pending on buffer)
                                handle_write();
                                // handle read
                                handle_read();
                            }else{
                                THINGER_LOG_ERROR("[%u] error on proxy connection: %s:%u (%s)", stream_id_, host_.c_str(), port_, ec.message().c_str());
                                handler(false);
                            }
                        });
    }

    bool proxy_session::stop(){
        if(socket_.is_open()) socket_.cancel();
        return true;
    }

    void proxy_session::write(uint8_t* buffer, size_t size){
        if(!size) return;

        // add data to buffer
        write_buffer_.emplace(std::string{reinterpret_cast<const char*>(buffer), size});

        // handle buffer writes
        handle_write();
    }

    void proxy_session::handle_write(){
        // ensure there is no any pending write, there is data to write, and the socket is open
        if(writing_ || write_buffer_.empty() || !socket_.is_open()) return;

        // mark proxy as writing
        writing_ = true;

        // get queue data
        const auto& front = write_buffer_.front();

        // write to socket
        //THINGER_LOG("writing on target proxy buffer: %zu bytes", front.size());
        socket_.async_write(front, [this, self = shared_from_this()](const boost::system::error_code& ec, std::size_t bytes_transferred){
            // error while writing to target connection
            if(ec){
                if(ec!=boost::asio::error::operation_aborted){
                    THINGER_LOG_ERROR("[%u] error while writing on proxy: %d (%s)", stream_id_, ec.value(), ec.message().c_str());
                    stop();
                }
                return;
            }

            increase_sent(bytes_transferred);
            //THINGER_LOG("wrote %zu bytes to %s:%u (%u) bytes", bytes_transferred, host_.c_str(), port_, stream_id_);

            // mark proxy as not writing
            writing_ = false;

            // there is pending data on queue
            if(!write_buffer_.empty()){
                // remove data from queue
                write_buffer_.pop();

                // handle write again
                handle_write();
            }
        });
    }

    void proxy_session::handle_read(){
        //THINGER_LOG("reading data from tcp proxy");
        socket_.async_read_some((uint8_t*)buffer_, READ_BUFFER_SIZE, [this, self = shared_from_this()](const boost::system::error_code& ec, std::size_t bytes_transferred){
            if(ec){
                if(ec!=boost::asio::error::operation_aborted){
                    THINGER_LOG_ERROR("[%u] error while reading from proxy after %lld (%s)", stream_id_, last_usage().count(), ec.message().c_str());
                    stop();
                }
                return;
            }
            //THINGER_LOG("read %zu bytes from %s:%u (%u) bytes", bytes_transferred, host_.c_str(), port_, stream_id_);
            if(bytes_transferred){
                increase_received(bytes_transferred);
                auto result = client_.stream_resource(stream_id_, reinterpret_cast<const uint8_t*>(&buffer_[0]), bytes_transferred);
                if(!result) {
                    THINGER_LOG_ERROR("[%u] cannot send proxy data for stream id", stream_id_);
                    stop();
                    return;
                }
            }
            handle_read();
        });
    }
}