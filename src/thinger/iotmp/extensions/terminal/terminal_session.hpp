#ifndef THINGER_IOTMP_TERMINAL_SESSION_HPP
#define THINGER_IOTMP_TERMINAL_SESSION_HPP

#include "../../client.hpp"
#include "../../core/iotmp_stream_session.hpp"
#include <boost/asio/posix/stream_descriptor.hpp>
#include <queue>

namespace thinger::iotmp {

constexpr size_t TERMINAL_BUFFER_SIZE = 1024;

class terminal_session : public stream_session {

public:
    terminal_session(client& client, uint16_t stream_id, std::string session, json_t& parameters);
    ~terminal_session() override;

    awaitable<exec_result> start() override;
    bool stop(StopReason reason = StopReason::SERVER_STOP) override;
    void handle_input(input& in) override;
    void update_params(input& in, output& out);

private:
    // Coroutine-based implementation
    awaitable<void> read_loop();
    awaitable<void> write_loop();

    int pid_ = 0;
    unsigned cols_ = 80;
    unsigned rows_ = 24;
    bool running_ = false;

    // Terminal command to use
    std::string terminal_;

    // Stream descriptor for PTY read/write
    boost::asio::posix::stream_descriptor descriptor_;

    // Read buffer
    uint8_t read_buffer_[TERMINAL_BUFFER_SIZE];

    // Write queue with synchronization
    std::queue<std::string> write_queue_;
    bool write_in_progress_ = false;
};

}

#endif