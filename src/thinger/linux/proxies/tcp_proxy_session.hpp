#ifndef THINGER_TCP_PROXY_SESSION_HPP
#define THINGER_TCP_PROXY_SESSION_HPP

#include "../sockets/tcp_socket.hpp"
#include "../asio_client.hpp"
#include "../streams/stream_session.hpp"
#include <string>
#include <queue>

namespace thinger_client {

#define READ_BUFFER_SIZE 4096

class tcp_proxy_session : public stream_session{

    public:
        tcp_proxy_session(asio_client& client, uint16_t stream_id, std::string session, std::string host, uint16_t port)
         : stream_session(client, stream_id, std::move(session)),
           socket_("tcp_proxy_session", client.get_io_service()),
           host_(std::move(host)),
           port_(port)
        {

        }

        bool start() override{
            socket_.connect(host_, std::to_string(port_), std::chrono::seconds{10}, [this, shelf = shared_from_this()](const boost::system::error_code & ec){
                if(!ec){
                    THINGER_LOG("target proxy connected: %s:%u", host_.c_str(), port_);
                    // handle write (if any data is pending on buffer)
                    handle_write();
                    // handle read
                    handle_read();
                }else{
                    THINGER_LOG_ERROR("error on proxy connection: %s:%u (%s)", host_.c_str(), port_, ec.message().c_str());
                }
            });
            return false;
        }

        bool stop() override{
            if(socket_.is_open()) socket_.cancel();
            return true;
        }

        void write(uint8_t* buffer, size_t size) override{
            if(!size) return;

            // add data to buffer
            write_buffer_.emplace(std::string{reinterpret_cast<const char*>(buffer), size});

            // handle buffer writes
            handle_write();
        }

        void handle_write(){
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
                        THINGER_LOG_ERROR("error while writing on proxy: %d (%s)", ec.value(), ec.message().c_str());
                        stop();
                    }
                    return;
                }

                increase_sent(bytes_transferred);
                //THINGER_LOG("wrote %zu bytes to %s:%u (%u) bytes", bytes_transferred, host_.c_str(), port_, stream_id_);

                // remove data from queue
                write_buffer_.pop();

                // mark proxy as not writing
                writing_ = false;

                // there is pending data on queue? handle_write again
                if(!write_buffer_.empty()) handle_write();
            });
        }

        void handle_read(){
            //THINGER_LOG("reading data from tcp proxy");
            socket_.async_read_some((uint8_t*)buffer_, READ_BUFFER_SIZE, [this, self = shared_from_this()](const boost::system::error_code& ec, std::size_t bytes_transferred){
                if(ec){
                    if(ec!=boost::asio::error::operation_aborted){
                        THINGER_LOG_ERROR("error while reading from proxy: %d (%s)", ec.value(), ec.message().c_str());
                        stop();
                    }
                    return;
                }
                //THINGER_LOG("read %zu bytes from %s:%u (%u) bytes", bytes_transferred, host_.c_str(), port_, stream_id_);
                if(bytes_transferred){
                    increase_received(bytes_transferred);
                    auto result = client_.stream_resource(stream_id_, reinterpret_cast<const uint8_t*>(&buffer_[0]), bytes_transferred);
                    if(!result) {
                        THINGER_LOG_ERROR("cannot send proxy data for stream id: %u", stream_id_);
                        stop();
                        return;
                    }
                }
                handle_read();
            });
        }

    private:
        char buffer_[READ_BUFFER_SIZE];
        std::queue<std::string> write_buffer_;
        ::thinger::tcp_socket socket_;
        std::string host_;
        uint16_t port_;
        bool writing_ = false;
    };
}

#endif