#include "file_download_session.hpp"
#include "../../core/iotmp_types.hpp"
#include <thinger/util/logger.hpp>
#include <boost/asio/post.hpp>

#ifndef THINGER_LOG_DEBUG
#define THINGER_LOG_DEBUG(...) ((void)0)
#endif

namespace thinger::iotmp{

    file_download_session::file_download_session(client& client, const transfer_config& config)
        : stream_session(client, config.stream_id, config.session_name)
        , file_path_(config.file_path)
        , chunk_size_(config.chunk_size)
        , window_size_(config.window_size)
        , max_bandwidth_mbps_(config.max_bandwidth_mbps)
    {
        buffer_.resize(chunk_size_);
    }

    awaitable<exec_result> file_download_session::start() {
        THINGER_LOG("starting file download: {}", file_path_.string());

        state_ = SessionState::STARTING;
        start_time_ = std::chrono::steady_clock::now();
        last_progress_time_ = start_time_;
        last_chunk_time_ = start_time_;

        // Check file exists and is readable
        if(!std::filesystem::exists(file_path_)) {
            state_ = SessionState::FAILED;
            error_message_ = "File not found";
            THINGER_LOG_ERROR("download: {} - {}", error_message_, file_path_.string());
            co_return exec_result{false, error_message_};
        }

        if(!std::filesystem::is_regular_file(file_path_)) {
            state_ = SessionState::FAILED;
            error_message_ = "Not a regular file";
            THINGER_LOG_ERROR("download: {} - {}", error_message_, file_path_.string());
            co_return exec_result{false, error_message_};
        }

        // Open file for reading
        file_stream_.open(file_path_, std::ios::in | std::ios::binary);
        if(!file_stream_.is_open()) {
            state_ = SessionState::FAILED;
            error_message_ = "Failed to open file for reading";
            THINGER_LOG_ERROR("download: {} - {}", error_message_, file_path_.string());
            co_return exec_result{false, error_message_};
        }

        file_size_ = std::filesystem::file_size(file_path_);
        THINGER_LOG("download started: {} - {} bytes (chunk size: {} bytes, window size: {} bytes)",
                   file_path_.filename().string(), file_size_, chunk_size_, window_size_);

        state_ = SessionState::IN_PROGRESS;

        // Keep session alive during the entire transfer
        self_reference_ = std::static_pointer_cast<file_download_session>(shared_from_this());

        // Return success with device parameters in PARAMETERS field
        // Server controls window_size, device confirms chunk_size it will use
        iotmp_message response(message::type::OK);
        response.params()["chunk_size"] = chunk_size_;
        response.payload()["chunk_size"] = chunk_size_;

        THINGER_LOG("download: sending start response - chunk_size={}",
                   chunk_size_);

        // Start sending data (post to avoid blocking)
        boost::asio::post(client_.get_io_context(), [this]() {
            send_next_chunk();
        });

        co_return exec_result(std::move(response));
    }

    bool file_download_session::stop(StopReason reason) {
        // Prevent double-stop
        if(stopping_) return false;
        stopping_ = true;
        
        // Cancel any pending timers
        if(ack_timer_) {
            ack_timer_->cancel();
            ack_timer_.reset();
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
            bandwidth_mbps = (bytes_transferred_ * 8.0) / (seconds * 1000000.0); // Mbps
        }
        
        // Determine final state based on reason and current state
        switch(reason) {
            case StopReason::SERVER_STOP:
                // Check if transfer was actually completed
                if(bytes_transferred_ >= file_size_ && file_size_ > 0) {
                    // Transfer was completed, this is a normal closure
                    state_ = SessionState::COMPLETED;
                    THINGER_LOG("download completed: {} - {} bytes in {:.2f} seconds ({:.2f} Mbps)",
                               file_path_.filename().string(), bytes_transferred_, seconds, bandwidth_mbps);
                } else {
                    // Transfer was stopped mid-transfer
                    state_ = SessionState::CANCELLED;
                    THINGER_LOG("download stopped by server: {}/{} bytes transferred",
                               bytes_transferred_, file_size_);
                }
                break;
            case StopReason::CLIENT_STOP:
                // Could be timeout, user cancel, or error - check state
                if(state_ == SessionState::FAILED || state_ == SessionState::TIMED_OUT) {
                    THINGER_LOG_ERROR("download failed: {} ({} bytes transferred)",
                               error_message_, bytes_transferred_);
                } else {
                    state_ = SessionState::CANCELLED;
                    THINGER_LOG("download cancelled: {}/{} bytes transferred",
                               bytes_transferred_, file_size_);
                }
                break;
            case StopReason::DISCONNECTED:
                state_ = SessionState::CANCELLED;
                THINGER_LOG_ERROR("download interrupted by disconnection: {}/{} bytes transferred",
                           bytes_transferred_, file_size_);
                break;
            default:
                state_ = SessionState::CANCELLED;
                THINGER_LOG("download stopped: {} bytes transferred", bytes_transferred_);
                break;
        }
        return true;
    }

    void file_download_session::handle_input(input& in) {
        // In download mode, data received is an ACK with format: {ack: N, bytes: M}
        if(in->is_object()) {
            size_t ack_bytes = in->value("bytes", static_cast<size_t>(0));
            increase_received(ack_bytes);
            if(ack_bytes > 0) {
                on_chunk_acknowledged(ack_bytes);
            } else {
                THINGER_LOG_DEBUG("ACK received without bytes field, using chunk_size as fallback");
                on_chunk_acknowledged(chunk_size_);
            }
        } else {
            LOG_WARNING("Unexpected data type in download session, expected JSON ACK");
        }
    }

    void file_download_session::send_next_chunk() {
        // STEP 1: Check if we should stop sending (EOF reached, file closed, or error state)
        if(state_ != SessionState::IN_PROGRESS || !file_stream_.is_open() || file_stream_.eof()) {
            THINGER_LOG("send_next_chunk: state={}, file_open={}, eof={}, bytes_in_flight={}",
                       static_cast<int>(state_), file_stream_.is_open(), file_stream_.eof(), bytes_in_flight_);

            // STEP 2: Wait for all in-flight data to be acknowledged before completing
            if(bytes_in_flight_ == 0) {
                // All chunks have been acknowledged
                if(state_ == SessionState::IN_PROGRESS) {
                    // STEP 3: First time detecting EOF - mark as completed but DON'T stop the session
                    // The server should close the stream when it's ready (protocol requirement)
                    state_ = SessionState::COMPLETED;
                    log_progress(true);  // Force log at 100%
                    THINGER_LOG("download transfer complete, waiting for server to close stream");

                    // STEP 4: Set a safety timeout in case server doesn't respond
                    // This prevents the session from hanging indefinitely
                    completion_timer_ = std::make_shared<boost::asio::steady_timer>(client_.get_io_context());
                    completion_timer_->expires_after(std::chrono::seconds(30)); // 30 seconds timeout
                    completion_timer_->async_wait([this](const boost::system::error_code& ec) {
                        if(!ec && self_reference_) {
                            THINGER_LOG_ERROR("timeout waiting for server to close stream, forcing stop");
                            state_ = SessionState::TIMED_OUT;
                                error_message_ = "Timeout waiting for server response";
                                stop(StopReason::CLIENT_STOP);
                        }
                    });
                } else if(state_ == SessionState::COMPLETED) {
                    // STEP 5: Already marked as completed, just waiting for server to close
                    return;
                } else {
                    // STEP 6: Some error or unexpected state, stop the transfer
                    stop(StopReason::CLIENT_STOP);
                }
            } else {
                // STEP 7: Still have chunks in flight, wait for ACKs before completing
                waiting_for_ack_ = true;
                start_ack_timeout();
            }
            return;
        }
        
        // STEP 8: Flow control - check if sending window is full
        // We limit the amount of unacknowledged data to prevent overwhelming the receiver
        if(bytes_in_flight_ >= window_size_) {
            // Window is full, must wait for ACKs before sending more
            THINGER_LOG_DEBUG("flow control: window full ({} bytes in flight, window size: {})",
                             bytes_in_flight_, window_size_);
            waiting_for_ack_ = true;
            start_ack_timeout();
            return;
        }
        
        // STEP 9: Read directly to json binary buffer (true zero-copy)
        json_t payload = json_t::binary(std::vector<uint8_t>(chunk_size_));
        auto& binary = payload.get_binary();
        file_stream_.read(reinterpret_cast<char*>(binary.data()), chunk_size_);
        size_t bytes_read = file_stream_.gcount();

        if(bytes_read > 0) {
            // Resize buffer to actual bytes read
            binary.resize(bytes_read);

            // STEP 10: Send the chunk
            client_.stream_resource(stream_id_, std::move(payload));
            bytes_transferred_ += bytes_read;
            increase_sent(bytes_read);
            bytes_in_flight_ += bytes_read;

            // Log progress every second for user feedback
            log_progress();

            // STEP 11: Decide whether to continue sending immediately or wait
            if(bytes_in_flight_ < window_size_) {
                // Still have room in the sending window

                // STEP 12: Apply bandwidth limiting if configured
                if(max_bandwidth_mbps_ > 0) {
                    // Calculate delay needed to maintain bandwidth limit
                    // Convert Mbps to bytes per millisecond for timing calculations
                    double bytes_per_ms = (max_bandwidth_mbps_ * 1000000.0 / 8.0) / 1000.0;
                    double ms_per_chunk = bytes_read / bytes_per_ms;

                    auto now = std::chrono::steady_clock::now();
                    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_chunk_time_).count();

                    if(elapsed_ms < ms_per_chunk) {
                        // Delay next chunk to respect bandwidth limit
                        auto delay_ms = static_cast<size_t>(ms_per_chunk - elapsed_ms);
                        if(delay_ms < MIN_CHUNK_DELAY_MS) delay_ms = MIN_CHUNK_DELAY_MS;

                        if(!rate_limit_timer_) {
                            rate_limit_timer_ = std::make_shared<boost::asio::steady_timer>(client_.get_io_context());
                        }

                        rate_limit_timer_->expires_after(std::chrono::milliseconds(delay_ms));
                        rate_limit_timer_->async_wait([this](const boost::system::error_code& ec) {
                            if(!ec) {
                                last_chunk_time_ = std::chrono::steady_clock::now();
                                send_next_chunk();
                            }
                        });
                        return;
                    }
                    last_chunk_time_ = now;
                }
                
                // STEP 13: No bandwidth limit - continue sending immediately
                // Post to io_context to avoid stack overflow from recursion
                boost::asio::post(client_.get_io_context(), [this](){
                    send_next_chunk();
                });
            } else {
                // STEP 14: Window is now full after sending, wait for ACKs
                waiting_for_ack_ = true;
                start_ack_timeout();
            }
        } else {
            // STEP 15: End of file reached (bytes_read == 0)
            THINGER_LOG("end of file reached, waiting for {} bytes to be acknowledged", bytes_in_flight_);
            if(bytes_in_flight_ == 0) {
                // STEP 16: All data sent and acknowledged, mark as completed but don't stop
                if(state_ == SessionState::IN_PROGRESS) {
                    state_ = SessionState::COMPLETED;
                    
                    // Set a timeout to force close if server doesn't respond
                    // (same logic as in STEP 4 above)
                    if(!completion_timer_) {
                        completion_timer_ = std::make_shared<boost::asio::steady_timer>(client_.get_io_context());
                        completion_timer_->expires_after(std::chrono::seconds(30));
                        completion_timer_->async_wait([this](const boost::system::error_code& ec) {
                            if(!ec && self_reference_) {
                                THINGER_LOG_ERROR("timeout waiting for server to close stream, forcing stop");
                                state_ = SessionState::TIMED_OUT;
                                error_message_ = "Timeout waiting for server response";
                                stop(StopReason::CLIENT_STOP);
                            }
                        });
                    }
                }
            } else {
                // STEP 17: Still waiting for final ACKs at EOF
                waiting_for_ack_ = true;
                start_ack_timeout();
            }
        }
    }

    void file_download_session::on_chunk_acknowledged(size_t ack_bytes) {
        // ACK contains the number of bytes confirmed by the server
        if(bytes_in_flight_ >= ack_bytes) {
            // Decrement the amount of unacknowledged data
            bytes_in_flight_ -= ack_bytes;

            THINGER_LOG_DEBUG("ACK received for {} bytes, {} bytes still in flight", ack_bytes, bytes_in_flight_);

            // Check if we can resume sending after receiving this ACK
            if(waiting_for_ack_ && (bytes_in_flight_ < window_size_)) {
                // Window has space again, stop waiting
                waiting_for_ack_ = false;
                
                // Cancel the ACK timeout timer since we got a response
                if(ack_timer_) {
                    ack_timer_->cancel();
                    ack_timer_.reset();
                }
                
                // Resume sending if we haven't reached EOF yet
                if(!file_stream_.eof() && state_ == SessionState::IN_PROGRESS) {
                    // More data to send - continue the transfer
                    // Use post to avoid deep recursion
                    boost::asio::post(client_.get_io_context(), [this](){
                        send_next_chunk();
                    });
                } else {
                    // We're at EOF, check if all data has been acknowledged
                    if(bytes_in_flight_ == 0) {
                        // All chunks acknowledged, and we're at EOF - transfer complete!
                        if(state_ == SessionState::IN_PROGRESS && bytes_transferred_ >= file_size_) {
                            // Mark as completed but DON'T stop the session
                            // Protocol requires server to close the stream
                            state_ = SessionState::COMPLETED;
                            log_progress(true);  // Force log at 100%
                            THINGER_LOG("download transfer complete, waiting for server to close stream");

                            // The self-reference keeps the session alive
                            // Set up one-time completion timer as safety net
                            if(!completion_timer_) {
                                completion_timer_ = std::make_shared<boost::asio::steady_timer>(client_.get_io_context());
                                completion_timer_->expires_after(std::chrono::seconds(30));
                                completion_timer_->async_wait([this](const boost::system::error_code& ec) {
                                    if(!ec && self_reference_) {
                                        THINGER_LOG_ERROR("timeout waiting for server to close stream, forcing stop");
                                        state_ = SessionState::TIMED_OUT;
                                error_message_ = "Timeout waiting for server response";
                                stop(StopReason::CLIENT_STOP);
                                    }
                                });
                            }
                        } else if(state_ != SessionState::COMPLETED) {
                            stop(StopReason::CLIENT_STOP);
                        }
                    }
                    // If bytes_in_flight_ > 0, we'll wait for more ACKs to arrive
                }
            }
        } else {
            THINGER_LOG_DEBUG("ACK received but no bytes in flight (possibly duplicate or late ACK)");
        }
    }

    void file_download_session::start_ack_timeout() {
        // Cancel any existing timer first
        if(ack_timer_) {
            ack_timer_->cancel();
        } else {
            ack_timer_ = std::make_shared<boost::asio::steady_timer>(client_.get_io_context());
        }

        // Set new timeout
        ack_timer_->expires_after(ACK_TIMEOUT);

        ack_timer_->async_wait([this](const boost::system::error_code& ec) {
            if(!ec && waiting_for_ack_ && ack_timer_) {
                // Timeout occurred while still waiting
                THINGER_LOG_ERROR("timeout waiting for ACKs after {} seconds, aborting transfer",
                                 ACK_TIMEOUT.count());
                state_ = SessionState::TIMED_OUT;
                error_message_ = "Timeout waiting for acknowledgments";
                ack_timer_.reset();
                stop(StopReason::CLIENT_STOP);
            }
            // If ec (cancelled) or !waiting_for_ack_ (ACKs received), do nothing
        });
    }

    void file_download_session::log_progress(bool force) {
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
            bandwidth_mbps = (bytes_transferred_ * 8.0) / (seconds * 1000000.0);
        }

        float progress = static_cast<float>(bytes_transferred_) / file_size_ * 100;
        THINGER_LOG("download progress: {:.1f}% ({}/{} bytes) - {:.2f} Mbps [{} KB in flight]",
                   progress, bytes_transferred_, file_size_, bandwidth_mbps, bytes_in_flight_ / 1024);
        last_progress_time_ = now;
    }

}