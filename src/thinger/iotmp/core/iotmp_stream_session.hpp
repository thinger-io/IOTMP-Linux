#ifndef THINGER_IOTMP_STREAM_SESSION_HPP
#define THINGER_IOTMP_STREAM_SESSION_HPP

#include <memory>
#include <functional>
#include <string>
#include <chrono>

// Forward declaration to avoid circular dependency
namespace thinger::iotmp {
    class client;
}

#include "iotmp_logger.hpp"
#include "iotmp_resource.hpp"

// Include coroutine support from thinger-http
#include <thinger/util/types.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>

namespace thinger::iotmp {

// Use coroutine types from thinger namespace (defined in thinger-http)
using thinger::awaitable;
using thinger::use_nothrow_awaitable;
using boost::asio::use_awaitable;
using boost::asio::co_spawn;
using boost::asio::detached;

// Stop reasons for stream sessions
enum class StopReason {
    SERVER_STOP,     // STOP_STREAM received from server
    CLIENT_STOP,     // Client closes stream voluntarily
    DISCONNECTED     // Connection lost
};

class stream_session : public std::enable_shared_from_this<stream_session> {

public:
    stream_session(client& client, uint16_t stream_id, std::string session = "")
        : client_(client),
          stream_id_(stream_id),
          session_(std::move(session)),
          last_(std::chrono::system_clock::now())
    {
    }

    virtual ~stream_session() {
        THINGER_LOG("stream session {} ended. {} sent, {} received", stream_id_, sent_, received_);
        if(on_end_) on_end_();
    }

    void set_on_end_listener(std::function<void()> listener) {
        on_end_ = std::move(listener);
    }

    // Pure virtual methods - sessions must implement these
    virtual void handle_input(input& in) = 0;
    virtual awaitable<exec_result> start() = 0;
    virtual bool stop(StopReason reason = StopReason::SERVER_STOP) = 0;

    uint16_t get_stream_id() const {
        return stream_id_;
    }

    const std::string& get_session() const {
        return session_;
    }

    size_t get_sent() const {
        return sent_;
    }

    size_t get_received() const {
        return received_;
    }

    std::chrono::milliseconds last_usage() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now() - last_);
    }

protected:
    void increase_sent(size_t bytes) {
        sent_ += bytes;
        last_ = std::chrono::system_clock::now();
    }

    void increase_received(size_t bytes) {
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
