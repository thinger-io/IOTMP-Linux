#include "terminal_session.hpp"

#include <boost/process.hpp>
#include <boost/asio.hpp>
#include <filesystem>
#include <thinger/util/logger.hpp>

#ifdef __linux__
#include <pty.h>
#elif __APPLE__
#include <util.h>
#endif

namespace thinger::iotmp {

terminal_session::terminal_session(client& client, uint16_t stream_id, std::string session,
                                   json_t& parameters)
    : stream_session(client, stream_id, std::move(session)),
      descriptor_(client.get_io_context())
{
    // Find available terminal
    std::vector<std::string> terminals{"zsh", "bash", "sh", "ash"};
    for(auto& terminal : terminals) {
        if(std::filesystem::exists("/bin/" + terminal)) {
            terminal_ = terminal;
            break;
        }
    }

    // Initialize cols and rows from parameters
    cols_ = get_value(parameters, "cols", 80u);
    rows_ = get_value(parameters, "rows", 24u);

    THINGER_LOG("received shell parameters: {}", parameters.dump());
}

terminal_session::~terminal_session() {
    THINGER_LOG("removing terminal: '{}' with {} (cols) {} (rows)", terminal_, cols_, rows_);
}

awaitable<exec_result> terminal_session::start() {
    if(pid_ || descriptor_.is_open() || terminal_.empty()) {
        co_return exec_result{false, "Terminal already running or not available"};
    }

    THINGER_LOG("initializing terminal '{}' with {} (cols) {} (rows)", terminal_, cols_, rows_);

    int master;
    char name[100];

    winsize ws;
    ws.ws_col = cols_;
    ws.ws_row = rows_;

    std::string term_path = "/bin/" + terminal_;
    THINGER_LOG("starting terminal: {} ({}) -> {} (cols) {} (rows)",
                term_path, terminal_, ws.ws_col, ws.ws_row);

    pid_ = forkpty(&master, name, nullptr, &ws);

    if(pid_ == 0) {
        // Child process
        setenv("TERM", "xterm-256color", 1);
        exit(execlp(term_path.c_str(), terminal_.c_str(), "-l", "-i", "-s", nullptr));

    } else if(pid_ > 0) {
        // Parent process
        THINGER_LOG("terminal started (pid {}): {}", pid_, name);
        descriptor_.assign(master);
        running_ = true;

        // Launch read loop
        co_spawn(descriptor_.get_executor(), read_loop(), detached);

        co_return true;
    }

    co_return exec_result{false, "Failed to fork terminal process"};
}

bool terminal_session::stop(StopReason reason) {
    running_ = false;
    bool success = true;

    // Stop terminal process
    if(pid_) {
        int result = kill(pid_, SIGHUP);
        THINGER_LOG("stopping terminal ({}): {}", pid_, result == 0 ? "ok" : "error");
        pid_ = 0;
        success &= (result == 0);
    }

    // Close descriptor
    if(descriptor_.is_open()) {
        boost::system::error_code ec;
        descriptor_.close(ec);
        success &= !ec;
    }

    return success;
}

void terminal_session::handle_input(input& in) {
    if(!running_) return;

    // Terminal expects binary data (keyboard input)
    if(!in->is_binary()) return;

    auto& binary = in->get_binary();
    if(binary.empty()) return;

    increase_received(binary.size());

    // Queue data for writing
    write_queue_.emplace(reinterpret_cast<const char*>(binary.data()), binary.size());

    // Start write loop if not already running
    if(!write_in_progress_) {
        write_in_progress_ = true;
        co_spawn(descriptor_.get_executor(), write_loop(), detached);
    }
}

awaitable<void> terminal_session::read_loop() {
    auto self = shared_from_this();

    while(running_ && descriptor_.is_open()) {
        auto [ec, bytes] = co_await descriptor_.async_read_some(
            boost::asio::buffer(read_buffer_, TERMINAL_BUFFER_SIZE),
            use_nothrow_awaitable);

        if(ec) {
            if(ec != boost::asio::error::operation_aborted) {
                THINGER_LOG_ERROR("error reading from terminal: {} ({})",
                                 ec.value(), ec.message());
            }
            stop();
            break;
        }

        if(bytes > 0) {
            increase_sent(bytes);

            if(!client_.stream_resource(stream_id_, read_buffer_, bytes)) {
                THINGER_LOG_ERROR("[{}] cannot send terminal data", stream_id_);
                stop();
                break;
            }
        }
    }
}

awaitable<void> terminal_session::write_loop() {
    auto self = shared_from_this();

    while(running_ && !write_queue_.empty() && descriptor_.is_open()) {
        auto data = std::move(write_queue_.front());
        write_queue_.pop();

        auto [ec, bytes] = co_await boost::asio::async_write(
            descriptor_,
            boost::asio::buffer(data),
            use_nothrow_awaitable);

        if(ec) {
            if(ec != boost::asio::error::operation_aborted) {
                THINGER_LOG_ERROR("error writing to terminal: {} ({})",
                                 ec.value(), ec.message());
            }
            stop();
            break;
        }
    }

    write_in_progress_ = false;
}

void terminal_session::update_params(input& in, output& out) {
    unsigned cols = get_value(in.payload(), "size.cols", 0u);
    unsigned rows = get_value(in.payload(), "size.rows", 0u);

    if(cols == 0 || rows == 0) {
        out["error"] = "rows and cols must be greater than 0";
        out.set_return_code(400);
    } else if(descriptor_.is_open()) {
        winsize ws;
        ws.ws_col = cols;
        ws.ws_row = rows;
        if(ioctl(descriptor_.native_handle(), TIOCSWINSZ, &ws) != 0) {
            out["error"] = strerror(errno);
            out.set_return_code(400);
        } else {
            cols_ = cols;
            rows_ = rows;
            THINGER_LOG("updated terminal size to {}x{} (cols, rows)", cols_, rows_);
        }
    } else {
        out["error"] = "terminal is not active";
        out.set_return_code(404);
    }
}

}
