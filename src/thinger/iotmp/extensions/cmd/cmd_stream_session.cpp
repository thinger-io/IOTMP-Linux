#include "cmd_stream_session.hpp"
#include "../../util/shell.hpp"

#include <boost/asio.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/process/v2/stdio.hpp>
#include <thinger/util/logger.hpp>

#include <chrono>
#include <utility>

namespace thinger::iotmp {

    using namespace boost::asio::experimental::awaitable_operators;

    cmd_stream_session::cmd_stream_session(client& client, uint16_t stream_id, std::string session,
                                           json_t& parameters)
        : stream_session(client, stream_id, std::move(session)),
          stdin_pipe_(client.get_io_context()),
          stdout_pipe_(client.get_io_context()),
          stderr_pipe_(client.get_io_context()),
          timeout_timer_(client.get_io_context())
    {
        ensure_home_env();
        command_ = get_value(parameters, "cmd", empty::string);
        timeout_seconds_ = get_value(parameters, "timeout", 0);
        THINGER_LOG("cmd stream {} created: '{}' (timeout: {}s)",
                    stream_id_, command_, timeout_seconds_);
    }

    cmd_stream_session::~cmd_stream_session() {
        // bp2::process destructor calls std::terminate() if ownership has not
        // been released. async_wait reaps the child but does not flip the
        // internal state to "released", so we must call detach() explicitly.
        if (process_) {
            process_->detach();
        }
        THINGER_LOG("cmd stream {} destroyed", stream_id_);
    }

    awaitable<exec_result> cmd_stream_session::start() {
        if (command_.empty()) {
            co_return exec_result{false, "no command provided"};
        }

        namespace bp2 = boost::process::v2;
        boost::system::error_code ec;
        std::vector<std::string> args = {"-c", command_};
        process_.emplace(
            client_.get_io_context().get_executor(),
            preferred_shell(),
            args,
            bp2::process_stdio{stdin_pipe_, stdout_pipe_, stderr_pipe_},
            ec
        );

        if (ec) {
            THINGER_LOG_ERROR("cmd stream {} failed to launch: {}", stream_id_, ec.message());
            process_.reset();
            co_return exec_result{false, ec.message()};
        }

        THINGER_LOG("cmd stream {} launched (pid {})", stream_id_, process_->id());

        // Spawn the coordinator coroutine; it keeps the session alive via
        // shared_from_this() until stdout, stderr, and the process all complete.
        co_spawn(client_.get_io_context(), run(), detached);

        if (timeout_seconds_ > 0) {
            co_spawn(client_.get_io_context(), watch_timeout(), detached);
        }

        co_return true;
    }

    bool cmd_stream_session::stop(StopReason reason) {
        stopped_externally_ = true;

        if (process_ && !finished_) {
            boost::system::error_code ec;
            process_->terminate(ec);
        }

        boost::system::error_code ec;
        stdin_pipe_.close(ec);
        stdout_pipe_.close(ec);
        stderr_pipe_.close(ec);
        timeout_timer_.cancel();

        return true;
    }

    void cmd_stream_session::handle_input(input& in) {
        if (finished_ || !stdin_pipe_.is_open()) return;

        // Accept either raw binary payloads or string payloads. Anything else
        // (objects, arrays, numbers) is ignored — the convention is that
        // stdin data is transported verbatim.
        const auto& payload = in.payload();
        std::string data;
        if (payload.is_binary()) {
            const auto& bin = payload.get_binary();
            data.assign(bin.begin(), bin.end());
        } else if (payload.is_string()) {
            data = payload.get<std::string>();
        } else {
            return;
        }
        if (data.empty()) return;

        increase_received(data.size());
        stdin_queue_.emplace(std::move(data));

        if (!stdin_writing_) {
            stdin_writing_ = true;
            co_spawn(client_.get_io_context(), stdin_write_loop(), detached);
        }
    }

    awaitable<void> cmd_stream_session::stdin_write_loop() {
        auto self = shared_from_this();
        while (!stdin_queue_.empty() && stdin_pipe_.is_open()) {
            auto data = std::move(stdin_queue_.front());
            stdin_queue_.pop();
            auto [ec, bytes] = co_await boost::asio::async_write(
                stdin_pipe_,
                boost::asio::buffer(data),
                use_nothrow_awaitable);
            if (ec) {
                if (ec != boost::asio::error::operation_aborted) {
                    LOG_WARNING("cmd stream {} stdin write failed: {}",
                                stream_id_, ec.message());
                }
                break;
            }
        }
        stdin_writing_ = false;
    }

    awaitable<void> cmd_stream_session::run() {
        auto self = shared_from_this();

        // Wait until stdout, stderr, and the process have all completed.
        // Reads finish on EOF (process closes its end) or operation_aborted
        // (stop() closes the pipes); the process completes via async_wait.
        co_await (read_stdout() && read_stderr() && wait_process());

        timeout_timer_.cancel();

        // Send the final exit frame and notify the server that the stream is
        // over, unless the server already requested the stop itself.
        json_t frame;
        frame["exit"] = exit_code_;
        if (timed_out_) frame["timeout"] = true;
        client_.stream_resource(stream_id_, std::move(frame));

        if (process_) {
            process_->detach();
        }
        // Note: we do NOT call client_.stop_stream() here. The session ending
        // triggers the stream_manager's on_end_ listener which already sends
        // STOP_STREAM via client_.stop_stream() when the session is gone.
        // Sending it from both paths produces a duplicate STOP_STREAM.
    }

    awaitable<void> cmd_stream_session::read_stdout() {
        auto self = shared_from_this();
        while (true) {
            auto [ec, bytes] = co_await stdout_pipe_.async_read_some(
                boost::asio::buffer(stdout_buffer_, CMD_STREAM_BUFFER_SIZE),
                use_nothrow_awaitable);
            if (ec) break;
            if (bytes > 0) {
                increase_sent(bytes);
                json_t frame;
                frame["out"] = json_t::binary({stdout_buffer_, stdout_buffer_ + bytes});
                client_.stream_resource(stream_id_, std::move(frame));
            }
        }
    }

    awaitable<void> cmd_stream_session::read_stderr() {
        auto self = shared_from_this();
        while (true) {
            auto [ec, bytes] = co_await stderr_pipe_.async_read_some(
                boost::asio::buffer(stderr_buffer_, CMD_STREAM_BUFFER_SIZE),
                use_nothrow_awaitable);
            if (ec) break;
            if (bytes > 0) {
                increase_sent(bytes);
                json_t frame;
                frame["err"] = json_t::binary({stderr_buffer_, stderr_buffer_ + bytes});
                client_.stream_resource(stream_id_, std::move(frame));
            }
        }
    }

    awaitable<void> cmd_stream_session::wait_process() {
        auto self = shared_from_this();
        if (!process_) co_return;

        namespace bp2 = boost::process::v2;
        auto [ec, native_code] = co_await process_->async_wait(use_nothrow_awaitable);
        exit_code_ = ec ? -1 : bp2::evaluate_exit_code(native_code);
        finished_ = true;
    }

    awaitable<void> cmd_stream_session::watch_timeout() {
        auto self = shared_from_this();
        timeout_timer_.expires_after(std::chrono::seconds(timeout_seconds_));
        auto [ec] = co_await timeout_timer_.async_wait(use_nothrow_awaitable);
        if (ec) co_return; // cancelled
        if (process_ && !finished_) {
            LOG_WARNING("cmd stream {} timed out after {}s, terminating",
                        stream_id_, timeout_seconds_);
            timed_out_ = true;
            boost::system::error_code term_ec;
            process_->terminate(term_ec);
        }
    }

}
