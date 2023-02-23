#ifndef THINGER_CLIENT_STREAM_SESSION_HPP
#define THINGER_CLIENT_STREAM_SESSION_HPP

#include <memory>
#include <functional>
#include <string>
#include "../../client.hpp"

namespace thinger::iotmp{

    class stream_session : public std::enable_shared_from_this<stream_session>{

    public:

        stream_session(client& client, uint16_t stream_id, std::string session = "")
        : client_(client),
          stream_id_(stream_id),
          session_(std::move(session)),
          last_(std::chrono::system_clock::now())
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
        virtual void start(result_handler) = 0;
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

        std::chrono::milliseconds last_usage() const{
            return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now()-last_);
        }

    protected:

        void increase_sent(size_t bytes){
            sent_ += bytes;
            last_ = std::chrono::system_clock::now();
        }

        void increase_received(size_t bytes){
            received_ += bytes;
            last_ = std::chrono::system_clock::now();
        }

    protected:
        client& client_;
        uint16_t stream_id_;
        std::string session_;
        std::chrono::time_point<std::chrono::system_clock> last_;
        size_t sent_ = 0;
        size_t received_ = 0;
        std::function<void()> on_end_;
    };

}

#endif