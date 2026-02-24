#ifndef THINGER_IOTMP_PROXY_SESSION_HPP
#define THINGER_IOTMP_PROXY_SESSION_HPP

#include "../../client.hpp"
#include "../../core/iotmp_stream_session.hpp"
#include <string>
#include <queue>

namespace thinger::iotmp {

constexpr size_t PROXY_BUFFER_SIZE = 4096;

class proxy_session : public stream_session {

public:
    proxy_session(client& client, uint16_t stream_id, std::string session,
                  std::string host, uint16_t port, bool secure);
    ~proxy_session() override;

    awaitable<exec_result> start() override;
    bool stop(StopReason reason = StopReason::SERVER_STOP) override;
    void handle_input(input& in) override;

private:
    awaitable<void> read_loop();
    awaitable<void> write_loop();

    std::shared_ptr<thinger::asio::socket> socket_;
    std::string host_;
    uint16_t port_;

    // Write queue with synchronization
    std::queue<std::string> write_queue_;
    bool write_in_progress_ = false;
    bool running_ = false;

    // Read buffer
    uint8_t read_buffer_[PROXY_BUFFER_SIZE];
};

}

#endif