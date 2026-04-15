#ifndef THINGER_IOTMP_CMD_STREAM_SESSION_HPP
#define THINGER_IOTMP_CMD_STREAM_SESSION_HPP

#include "../../client.hpp"
#include "../../core/iotmp_stream_session.hpp"

#include <boost/asio/readable_pipe.hpp>
#include <boost/asio/writable_pipe.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/process/v2/process.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <queue>
#include <string>

namespace thinger::iotmp {

    constexpr size_t CMD_STREAM_BUFFER_SIZE = 4096;

    class cmd_stream_session : public stream_session {

    public:
        cmd_stream_session(client& client, uint16_t stream_id, std::string session, json_t& parameters);
        ~cmd_stream_session() override;

        awaitable<exec_result> start() override;
        bool stop(StopReason reason = StopReason::SERVER_STOP) override;
        void handle_input(input& in) override;

    private:
        awaitable<void> run();
        awaitable<void> read_stdout();
        awaitable<void> read_stderr();
        awaitable<void> wait_process();
        awaitable<void> watch_timeout();
        awaitable<void> stdin_write_loop();

        std::string command_;
        int timeout_seconds_ = 0;
        bool timed_out_ = false;
        bool finished_ = false;
        bool stopped_externally_ = false;
        int exit_code_ = -1;

        boost::asio::writable_pipe stdin_pipe_;
        boost::asio::readable_pipe stdout_pipe_;
        boost::asio::readable_pipe stderr_pipe_;
        boost::asio::steady_timer timeout_timer_;
        std::optional<boost::process::v2::process> process_;

        uint8_t stdout_buffer_[CMD_STREAM_BUFFER_SIZE];
        uint8_t stderr_buffer_[CMD_STREAM_BUFFER_SIZE];

        std::queue<std::string> stdin_queue_;
        bool stdin_writing_ = false;
    };

}

#endif
