#ifndef THINGER_CLIENT_STREAM_SESSION_HPP
#define THINGER_CLIENT_STREAM_SESSION_HPP

#include <memory>
#include <functional>
#include <string>
#include "../asio_client.hpp"

namespace thinger_client{

    class stream_session : public std::enable_shared_from_this<stream_session>{

    public:

        stream_session(asio_client& client, uint16_t stream_id, std::string session = "")
        : client_(client), stream_id_(stream_id), session_(std::move(session))
        {

        }

        virtual ~stream_session(){
            THINGER_LOG("stream session %u ended. %zu sent, %zu received", stream_id_, sent_, received_);
            if(on_end_) on_end_();
        }

        void set_on_end_listener(std::function<void()> listener){
            on_end_ = listener;
        }

        virtual void write(uint8_t* buffer, size_t size) = 0;
        virtual bool start() = 0;
        virtual bool stop() = 0;

        uint16_t get_stream_id() const{
            return stream_id_;
        }

        const std::string& get_session() const{
            return session_;
        }

        size_t get_sent() const{
            return sent_;
        }

        size_t get_received() const{
            return received_;
        }

    protected:

        void increase_sent(size_t bytes){
            sent_ += bytes;
        }

        void increase_received(size_t bytes){
            received_ += bytes;
        }

    protected:
        asio_client& client_;
        uint16_t stream_id_;
        std::string session_;
        std::function<void()> on_end_;
        size_t sent_ = 0;
        size_t received_ = 0;
    };

}

#endif