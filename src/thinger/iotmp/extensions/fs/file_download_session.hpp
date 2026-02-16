#ifndef THINGER_IOTMP_FILE_DOWNLOAD_SESSION_HPP
#define THINGER_IOTMP_FILE_DOWNLOAD_SESSION_HPP

#include "../../core/iotmp_stream_session.hpp"
#include "filesystem.hpp"  // For DEFAULT_CHUNK_SIZE
#include <filesystem>
#include <fstream>
#include <vector>
#include <chrono>
#include <boost/asio/steady_timer.hpp>

namespace thinger::iotmp{

    class file_download_session : public stream_session {
    public:
        file_download_session(client& client, const transfer_config& config);
        ~file_download_session() override = default;

        // Override virtual methods from stream_session
        awaitable<exec_result> start() override;
        bool stop(StopReason reason = StopReason::SERVER_STOP) override;
        void handle_input(input& in) override;  // For receiving ACKs

    private:
        void send_next_chunk();
        void on_chunk_acknowledged(size_t ack_bytes);
        void start_ack_timeout();
        void log_progress(bool force = false);

    private:
        std::filesystem::path file_path_;
        std::ifstream file_stream_;  // Read-only for download
        
        // Transfer state
        size_t file_size_ = 0;
        size_t bytes_transferred_ = 0;
        size_t chunk_size_ = DEFAULT_CHUNK_SIZE;
        std::vector<uint8_t> buffer_;
        
        // Flow control for download (byte-based window)
        size_t bytes_in_flight_ = 0;  // Bytes sent but not yet acknowledged
        size_t window_size_ = 2 * 1024 * 1024;  // Default 2MB, can be set by server
        bool waiting_for_ack_ = false;
        std::shared_ptr<boost::asio::steady_timer> ack_timer_;
        
        // Transfer metrics
        std::chrono::steady_clock::time_point start_time_;
        std::chrono::steady_clock::time_point last_progress_time_;
        
        // Bandwidth limiting
        size_t max_bandwidth_mbps_ = 0;  // 0 = unlimited
        std::chrono::steady_clock::time_point last_chunk_time_;
        std::shared_ptr<boost::asio::steady_timer> rate_limit_timer_;
        
        // State management
        SessionState state_ = SessionState::IDLE;
        bool stopping_ = false;
        std::string error_message_;
        
        // Keep session alive after completion
        std::shared_ptr<file_download_session> self_reference_;
        std::shared_ptr<boost::asio::steady_timer> completion_timer_;
    };

}

#endif