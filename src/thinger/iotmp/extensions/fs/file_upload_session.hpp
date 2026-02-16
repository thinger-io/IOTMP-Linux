#ifndef THINGER_IOTMP_FILE_UPLOAD_SESSION_HPP
#define THINGER_IOTMP_FILE_UPLOAD_SESSION_HPP

#include "../../core/iotmp_stream_session.hpp"
#include "filesystem.hpp"  // For SessionState and StopReason
#include <filesystem>
#include <fstream>
#include <chrono>
#include <boost/asio/steady_timer.hpp>

namespace thinger::iotmp{

    class file_upload_session : public stream_session {
    public:
        file_upload_session(client& client, const transfer_config& config);
        ~file_upload_session() override = default;

        // Override virtual methods from stream_session
        awaitable<exec_result> start() override;
        bool stop(StopReason reason = StopReason::SERVER_STOP) override;
        void handle_input(input& in) override;  // For receiving file data

    private:
        void handle_upload_chunk(uint8_t* data, size_t size);
        void send_ack(bool is_final = false);
        void start_receive_timeout();
        void log_progress(bool force = false);

    private:
        std::filesystem::path file_path_;
        std::ofstream file_stream_;  // Write-only for upload

        // Transfer state
        size_t bytes_received_ = 0;
        size_t expected_size_ = 0;  // If known from metadata
        size_t chunk_size_ = DEFAULT_CHUNK_SIZE;

        // Transfer metrics
        std::chrono::steady_clock::time_point start_time_;
        std::chrono::steady_clock::time_point last_progress_time_;
        std::chrono::steady_clock::time_point last_ack_time_;
        std::chrono::steady_clock::time_point last_data_time_;

        // ACK control
        size_t bytes_since_last_ack_ = 0;
        uint32_t ack_number_ = 0;
        size_t ack_threshold_ = 0;  // Dynamic threshold calculated from file size

        // Bandwidth limiting
        size_t max_bandwidth_mbps_ = 0;  // 0 = unlimited
        std::chrono::steady_clock::time_point last_ack_sent_time_;
        std::shared_ptr<boost::asio::steady_timer> rate_limit_timer_;

        // Timeout detection
        std::chrono::seconds receive_timeout_{30};  // Dynamic timeout based on bandwidth
        std::shared_ptr<boost::asio::steady_timer> receive_timeout_timer_;

        // State management
        SessionState state_ = SessionState::IDLE;
        bool stopping_ = false;
        std::string error_message_;

        // Keep session alive after completion
        std::shared_ptr<file_upload_session> self_reference_;
        std::shared_ptr<boost::asio::steady_timer> completion_timer_;
    };

}

#endif