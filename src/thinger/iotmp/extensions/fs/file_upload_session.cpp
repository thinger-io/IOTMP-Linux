#include "file_upload_session.hpp"
#include <thinger/util/logger.hpp>
#include <boost/asio/post.hpp>

#ifndef THINGER_LOG_DEBUG
#define THINGER_LOG_DEBUG(...) ((void)0)
#endif

namespace thinger::iotmp{

    // Default timeout for receiving data during upload
    static constexpr std::chrono::seconds DEFAULT_RECEIVE_TIMEOUT{30};

    file_upload_session::file_upload_session(client& client, const transfer_config& config)
        : stream_session(client, config.stream_id, config.session_name)
        , file_path_(config.file_path)
        , expected_size_(config.expected_size)
        , chunk_size_(config.chunk_size)
        , max_bandwidth_mbps_(config.max_bandwidth_mbps)
    {
    }

    awaitable<exec_result> file_upload_session::start() {
        THINGER_LOG("starting file upload: {}", file_path_.string());

        state_ = SessionState::STARTING;
        start_time_ = std::chrono::steady_clock::now();
        last_progress_time_ = start_time_;
        last_ack_time_ = start_time_;
        last_data_time_ = start_time_;
        last_ack_sent_time_ = start_time_;

        // Create parent directories if needed
        std::filesystem::create_directories(file_path_.parent_path());

        // Open file for writing
        file_stream_.open(file_path_, std::ios::out | std::ios::binary | std::ios::trunc);
        if(!file_stream_.is_open()) {
            state_ = SessionState::FAILED;
            error_message_ = "Failed to open file for writing";
            THINGER_LOG_ERROR("upload: {} - {}", error_message_, file_path_.string());
            co_return exec_result{false, error_message_};
        }

        // Calculate dynamic receive timeout based on chunk size and bandwidth
        // Timeout must be longer than the time needed to process a chunk with bandwidth limiting
        receive_timeout_ = DEFAULT_RECEIVE_TIMEOUT;

        if(max_bandwidth_mbps_ > 0) {
            // Time needed for one chunk at the configured bandwidth (in seconds)
            // chunk_size (bytes) * 8 (bits/byte) / (bandwidth (Mbps) * 1000000 (bits/Mbps))
            double chunk_time_seconds = (chunk_size_ * 8.0) / (max_bandwidth_mbps_ * 1000000.0);

            // Add 50% safety margin to account for processing time and network delays
            int64_t required_timeout_seconds = static_cast<int64_t>(chunk_time_seconds * 1.5) + 10;

            // Use the larger of default or calculated timeout
            int64_t default_timeout_seconds = DEFAULT_RECEIVE_TIMEOUT.count();
            int64_t final_timeout = std::max(default_timeout_seconds, required_timeout_seconds);
            receive_timeout_ = std::chrono::seconds{final_timeout};

            if(required_timeout_seconds > DEFAULT_RECEIVE_TIMEOUT.count()) {
                THINGER_LOG("upload: adjusted timeout to {} seconds for chunk size {} bytes @ {} Mbps",
                           final_timeout, chunk_size_, max_bandwidth_mbps_);
            }
        }

        // Calculate adaptive ACK threshold based on file size
        ack_threshold_ = calculate_ack_threshold(expected_size_, chunk_size_);

        THINGER_LOG("upload started: {} - {} bytes (chunk size: {} bytes, ACK threshold: {} bytes)",
                   file_path_.filename().string(), expected_size_, chunk_size_, ack_threshold_);
        state_ = SessionState::IN_PROGRESS;

        // Keep session alive during the entire transfer
        self_reference_ = std::static_pointer_cast<file_upload_session>(shared_from_this());

        // Start timeout detection
        start_receive_timeout();

        // Return success with device parameters in PARAMETERS field
        // The server controls window_size (how much it can buffer)
        iotmp_message response(message::type::OK);
        response.params()["chunk_size"] = chunk_size_;
        response.payload()["chunk_size"] = chunk_size_;

        THINGER_LOG("upload: sending start response - chunk_size={}",
                   chunk_size_);

        co_return exec_result(std::move(response));
    }

    bool file_upload_session::stop(StopReason reason) {
        // Prevent double-stop
        if(stopping_) return false;
        stopping_ = true;

        // Cancel any pending timers
        if(receive_timeout_timer_) {
            receive_timeout_timer_->cancel();
            receive_timeout_timer_.reset();
        }

        if(completion_timer_) {
            completion_timer_->cancel();
            completion_timer_.reset();
        }

        if(rate_limit_timer_) {
            rate_limit_timer_->cancel();
            rate_limit_timer_.reset();
        }

        // Release self-reference to allow destruction
        self_reference_.reset();

        if(file_stream_.is_open()) {
            file_stream_.close();
        }

        auto duration = std::chrono::steady_clock::now() - start_time_;
        auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
        double seconds = milliseconds / 1000.0;

        // Calculate effective bandwidth
        double bandwidth_mbps = 0;
        if(seconds > 0) {
            bandwidth_mbps = (bytes_received_ * 8.0) / (seconds * 1000000.0); // Mbps
        }

        // Determine final state based on reason and current state
        switch(reason) {
            case StopReason::SERVER_STOP:
                // Server stopped the upload - check if it was complete or cancelled
                if(state_ == SessionState::COMPLETED) {
                    THINGER_LOG("upload completed: {} - {} bytes in {:.2f} seconds ({:.2f} Mbps)",
                               file_path_.filename().string(), bytes_received_, seconds, bandwidth_mbps);
                } else {
                    state_ = SessionState::CANCELLED;
                    THINGER_LOG("upload stopped by server: {} bytes received",
                               bytes_received_);
                    // Clean up partial upload
                    if(std::filesystem::exists(file_path_)) {
                        std::filesystem::remove(file_path_);
                    }
                }
                break;
            case StopReason::CLIENT_STOP:
                // Client stopped - could be error, timeout, or user cancellation
                // State should already be set appropriately before calling stop
                if(state_ == SessionState::FAILED || state_ == SessionState::TIMED_OUT) {
                    THINGER_LOG_ERROR("upload failed: {} ({} bytes received)",
                               error_message_, bytes_received_);
                } else {
                    state_ = SessionState::CANCELLED;
                    THINGER_LOG("upload cancelled: {} bytes received", bytes_received_);
                }
                // Clean up partial upload
                if(std::filesystem::exists(file_path_)) {
                    std::filesystem::remove(file_path_);
                }
                break;
            case StopReason::DISCONNECTED:
                state_ = SessionState::CANCELLED;
                THINGER_LOG_ERROR("upload interrupted by disconnection: {} bytes received",
                           bytes_received_);
                // Clean up partial upload
                if(std::filesystem::exists(file_path_)) {
                    std::filesystem::remove(file_path_);
                }
                break;
            default:
                state_ = SessionState::CANCELLED;
                THINGER_LOG("upload stopped: {} bytes received", bytes_received_);
                // Clean up partial upload
                if(std::filesystem::exists(file_path_)) {
                    std::filesystem::remove(file_path_);
                }
                break;
        }
        return true;
    }

    void file_upload_session::handle_input(input& in) {
        // In upload mode, we receive binary file data
        if(in->is_binary()) {
            auto& binary = in->get_binary();
            if(!binary.empty()) {
                handle_upload_chunk(binary.data(), binary.size());
            }
        } else {
            LOG_WARNING("Unexpected data type in upload session, expected binary");
        }
    }

    void file_upload_session::handle_upload_chunk(uint8_t* data, size_t size) {
        if(state_ != SessionState::IN_PROGRESS || !file_stream_.is_open()) {
            return;
        }

        // Update last data received time
        auto now = std::chrono::steady_clock::now();
        last_data_time_ = now;

        // Write to file
        file_stream_.write(reinterpret_cast<const char*>(data), size);
        if(!file_stream_.good()) {
            state_ = SessionState::FAILED;
            error_message_ = "Failed to write to file";
            THINGER_LOG_ERROR("upload error: {}", error_message_);
            stop(StopReason::CLIENT_STOP);
            return;
        }

        bytes_received_ += size;
        bytes_since_last_ack_ += size;
        increase_received(size);

        // Log progress every second
        log_progress();

        // Reset receive timeout since we got data
        start_receive_timeout();

        // Send ACK when threshold is reached or on last chunk
        bool is_last_chunk = (expected_size_ > 0 && bytes_received_ >= expected_size_);
        if(bytes_since_last_ack_ >= ack_threshold_ || is_last_chunk) {
            send_ack(is_last_chunk);
        }

        // Check if upload is complete AFTER sending the final ACK
        if(is_last_chunk) {
            state_ = SessionState::COMPLETED;
            log_progress(true);  // Force log at 100%
            THINGER_LOG("upload transfer complete, waiting for server to close stream");
            // Don't stop - wait for server to close the stream (protocol requirement)
            return;
        }
    }

    void file_upload_session::send_ack(bool is_final) {
        auto now = std::chrono::steady_clock::now();

        // Apply bandwidth limiting if configured (but not for final ACK)
        if(max_bandwidth_mbps_ > 0 && !is_final) {
            // Calculate how much time should have passed for the bytes we received
            // Convert Mbps to bytes per millisecond for timing calculations
            double bytes_per_ms = (max_bandwidth_mbps_ * 1000000.0 / 8.0) / 1000.0;
            double ms_for_received_bytes = bytes_since_last_ack_ / bytes_per_ms;

            // Check how much time has actually passed since last ACK
            auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - last_ack_sent_time_).count();

            if(elapsed_ms < ms_for_received_bytes) {
                // We need to delay the ACK to respect bandwidth limit
                auto delay_ms = static_cast<size_t>(ms_for_received_bytes - elapsed_ms);
                if(delay_ms < MIN_CHUNK_DELAY_MS) delay_ms = MIN_CHUNK_DELAY_MS;

                if(!rate_limit_timer_) {
                    rate_limit_timer_ = std::make_shared<boost::asio::steady_timer>(client_.get_io_context());
                }

                rate_limit_timer_->expires_after(std::chrono::milliseconds(delay_ms));
                rate_limit_timer_->async_wait([this, bytes_to_ack = bytes_since_last_ack_](const boost::system::error_code& ec) {
                    if(!ec) {
                        // Actually send the ACK after the delay
                        json_t ack_data = {
                            {"ack", ++ack_number_},
                            {"bytes", bytes_to_ack}
                        };
                        client_.stream_resource(stream_id_, std::move(ack_data));
                        increase_sent(ack_data.dump().size());

                        bytes_since_last_ack_ = 0;
                        last_ack_time_ = std::chrono::steady_clock::now();
                        last_ack_sent_time_ = std::chrono::steady_clock::now();
                    }
                });
                return; // Don't send ACK immediately
            }
        }

        // No bandwidth limit or enough time has passed - send ACK immediately
        json_t ack_data = {
            {"ack", ++ack_number_},
            {"bytes", bytes_since_last_ack_}
        };
        client_.stream_resource(stream_id_, std::move(ack_data));
        increase_sent(ack_data.dump().size());

        bytes_since_last_ack_ = 0;
        last_ack_time_ = now;
        last_ack_sent_time_ = now;
    }

    void file_upload_session::start_receive_timeout() {
        // Cancel any existing timer first
        if(receive_timeout_timer_) {
            receive_timeout_timer_->cancel();
        } else {
            receive_timeout_timer_ = std::make_shared<boost::asio::steady_timer>(client_.get_io_context());
        }

        // Set new timeout - wait for data to arrive (use calculated timeout)
        receive_timeout_timer_->expires_after(receive_timeout_);

        receive_timeout_timer_->async_wait([this](const boost::system::error_code& ec) {
            if(!ec && receive_timeout_timer_) {
                // Timeout occurred while waiting for data
                auto elapsed = std::chrono::steady_clock::now() - last_data_time_;
                auto elapsed_seconds = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();

                THINGER_LOG_ERROR("timeout waiting for data after {} seconds, aborting upload",
                                 elapsed_seconds);
                state_ = SessionState::TIMED_OUT;
                error_message_ = "Timeout waiting for data";
                receive_timeout_timer_.reset();
                stop(StopReason::CLIENT_STOP);
            }
            // If ec (cancelled), do nothing - this is normal when we receive data
        });
    }

    void file_upload_session::log_progress(bool force) {
        auto now = std::chrono::steady_clock::now();

        // Check if we should log (every second or forced)
        if(!force && std::chrono::duration_cast<std::chrono::seconds>(now - last_progress_time_).count() < 1) {
            return;
        }

        // Calculate instantaneous bandwidth
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time_).count();
        double seconds = duration / 1000.0;
        double bandwidth_mbps = 0;
        if(seconds > 0) {
            bandwidth_mbps = (bytes_received_ * 8.0) / (seconds * 1000000.0);
        }

        if(expected_size_ > 0) {
            float progress = static_cast<float>(bytes_received_) / expected_size_ * 100;
            THINGER_LOG("upload progress: {:.1f}% ({}/{} bytes) - {:.2f} Mbps",
                       progress, bytes_received_, expected_size_, bandwidth_mbps);
        } else {
            THINGER_LOG("upload progress: {} bytes received - {:.2f} Mbps", bytes_received_, bandwidth_mbps);
        }
        last_progress_time_ = now;
    }

}