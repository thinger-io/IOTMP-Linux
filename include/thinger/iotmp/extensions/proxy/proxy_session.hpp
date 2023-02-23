#ifndef THINGER_IOTMP_PROXY_SESSION_HPP
#define THINGER_IOTMP_PROXY_SESSION_HPP

#include "../../client.hpp"
#include "../streams/stream_session.hpp"
#include <string>
#include <queue>

namespace thinger::iotmp {

#define READ_BUFFER_SIZE 4096

class proxy_session : public stream_session{

    public:
        proxy_session(client& client, uint16_t stream_id, std::string session, std::string host, uint16_t port);
        ~proxy_session() override;

        void start(result_handler handler) override;
        bool stop() override;
        void write(uint8_t* buffer, size_t size) override;
        void handle_write();
        void handle_read();

    private:
        char buffer_[READ_BUFFER_SIZE];
        std::queue<std::string> write_buffer_;
        thinger::asio::tcp_socket socket_;
        std::string host_;
        uint16_t port_;
        bool writing_ = false;
    };
}

#endif