#ifndef THINGER_IOTMP_FILESYSTEM_HPP
#define THINGER_IOTMP_FILESYSTEM_HPP

#include "../../client.hpp"
#include "../../core/iotmp_resource.hpp"
#include "../../core/iotmp_stream_manager.hpp"
#include <filesystem>
#include <chrono>
#include <string>
#include <memory>

namespace thinger::iotmp{

    // Session states for file transfers
    enum class SessionState {
        IDLE,           // Session created but not started
        STARTING,       // Starting (opening file, validations)
        IN_PROGRESS,    // Active transfer
        COMPLETED,      // Transfer completed successfully
        CANCELLED,      // Cancelled by user or server
        FAILED,         // Error during transfer
        TIMED_OUT       // Timeout waiting for ACKs or data
    };


    // File transfer constants
    static constexpr size_t DEFAULT_CHUNK_SIZE = 64 * 1024;   // 64KB default chunk size for streaming
    static constexpr size_t MAX_INLINE_FILE_SIZE = 64 * 1024; // 64KB max for inline transfers
    static constexpr size_t DEFAULT_WINDOW_SIZE = 2 * 1024 * 1024;  // 2MB default window

    // Flow control constants for streaming transfers
    // Note: window_size is controlled by the server (not the device) for both upload and download
    // - Upload: server controls how much it can buffer before sending to device
    // - Download: server tells device its window_size in start parameters
    static constexpr std::chrono::seconds ACK_TIMEOUT{10};     // Timeout waiting for acknowledgments

    // Bandwidth limiting constants (optional)
    static constexpr size_t DEFAULT_MAX_BANDWIDTH_MBPS = 0;    // 0 = unlimited, otherwise Mbps limit
    static constexpr size_t MIN_CHUNK_DELAY_MS = 1;           // Minimum delay between chunks in ms

    // ACK threshold limits
    static constexpr size_t MAX_ACK_THRESHOLD = 1 * 1024 * 1024;  // Maximum 1MB between ACKs

    /**
     * Calculate adaptive ACK threshold based on file size
     * Returns ~1% of file size with min/max bounds
     * @param file_size Total size of the file being transferred
     * @param chunk_size Size of individual chunks
     * @return Threshold in bytes for sending ACKs
     */
    inline size_t calculate_ack_threshold(size_t file_size, size_t chunk_size) {
        if(file_size == 0) {
            // Unknown file size - use default threshold
            return chunk_size * 4;  // ACK every 4 chunks
        }

        // ACK every ~1% of file size
        size_t threshold = file_size / 100;

        // Minimum: one chunk (for very small files, ensures at least some progress)
        if(threshold < chunk_size) {
            threshold = chunk_size;
        }

        // Maximum: 1MB (avoid too much buffering and provide regular feedback)
        if(threshold > MAX_ACK_THRESHOLD) {
            threshold = MAX_ACK_THRESHOLD;
        }

        return threshold;
    }

    // Configuration struct for file transfer sessions (upload and download)
    struct transfer_config {
        uint16_t stream_id = 0;
        std::string session_name;
        std::filesystem::path file_path;
        size_t expected_size = 0;           // For uploads: file size to receive
        size_t chunk_size = DEFAULT_CHUNK_SIZE;      // Device decides chunk size
        size_t window_size = DEFAULT_WINDOW_SIZE;    // Server controls window size
        size_t max_bandwidth_mbps = DEFAULT_MAX_BANDWIDTH_MBPS;
    };

    class filesystem;
    
    class filesystem_downloads : public stream_manager {
    public:
        explicit filesystem_downloads(client& client, filesystem& fs);

    protected:
        std::shared_ptr<stream_session> create_session(client& client, uint16_t stream_id,
                                                       std::string session, json_t& parameters) override;
    private:
        filesystem& fs_;
    };

    class filesystem_uploads : public stream_manager {
    public:
        explicit filesystem_uploads(client& client, filesystem& fs);

    protected:
        std::shared_ptr<stream_session> create_session(client& client, uint16_t stream_id,
                                                       std::string session, json_t& parameters) override;
    private:
        filesystem& fs_;
    };

    class filesystem {

    public:
        explicit filesystem(client& client, const std::filesystem::path& base_path = std::filesystem::current_path());
        ~filesystem() = default;

        void set_base_path(const std::filesystem::path& path);
        const std::filesystem::path& get_base_path() const { return base_path_; }

    private:
        void handle_get(input& in, output& out);  // Unified GET handler
        void handle_list(input& in, output& out);
        void handle_info(input& in, output& out);
        void handle_download(input& in, output& out);  // For small files (inline transfer)
        void handle_put(input& in, output& out);  // For small files (inline upload) and directories
        void handle_delete(input& in, output& out);
        void handle_move(input& in, output& out);
        void handle_hash(input& in, output& out);

        bool is_within_base_path(const std::filesystem::path& path) const;
        bool resolve_and_validate_path(input& in, output& out, std::filesystem::path& resolved_path,
                                       const char* wildcard_name = "path", bool check_exists = true);
        
    public:
        std::filesystem::path resolve_path(const std::string& input_path) const;
        bool validate_resolved_path(const std::filesystem::path& path) const;
        std::string get_file_type(const std::filesystem::file_status& status) const;
        std::string format_permissions(const std::filesystem::perms& p) const;

    private:
        client& client_;
        std::filesystem::path base_path_;
        std::unique_ptr<filesystem_downloads> downloads_manager_;
        std::unique_ptr<filesystem_uploads> uploads_manager_;
    };

}

#endif