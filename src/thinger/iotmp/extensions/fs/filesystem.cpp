#include "filesystem.hpp"
#include "file_download_session.hpp"
#include "file_upload_session.hpp"
#include <thinger/util/logger.hpp>
#include <sys/stat.h>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <vector>

namespace thinger::iotmp{

    filesystem::filesystem(client& client, const std::filesystem::path& base_path)
        : client_(client), base_path_(base_path)
    {
        // Base path acts as the root for all operations
        if(base_path_.empty()) {

            THINGER_LOG("initializing filesystem extension (unrestricted access)");
        } else {
            THINGER_LOG("initializing filesystem extension with base path: {}", base_path_.string());
        }

        // Register unified $fs/get resource with dynamic URL for listing and downloads
        // - GET $fs/get/path/to/dir/  → list directory
        // - GET $fs/get/path/to/file  → download file (inline)
        client["$fs/get/*path"] = [&](input& in, output& out){
            handle_get(in, out);
        };

        // Register $fs/info resource for getting file/directory information
        client["$fs/info/*path"] = [&](input& in, output& out){
            handle_info(in, out);
        };
        
        // Register $fs/put resource for small file uploads (inline, no streaming)
        client["$fs/put/*path"] = [&](input& in, output& out){
            handle_put(in, out);
        };

        // Register $fs/delete resource for file/directory deletion
        client["$fs/delete/*path"] = [&](input& in, output& out){
            handle_delete(in, out);
        };

        // Note: mkdir functionality is now part of $fs/put
        // Use PUT with path ending in '/' to create directories

        // Register $fs/move resource for file/directory move/rename
        // Source comes from wildcard, destination from parameters
        client["$fs/move/*source"] = [&](input& in, output& out){
            handle_move(in, out);
        };
        
        // Register stream handlers for file transfers
        downloads_manager_ = std::make_unique<filesystem_downloads>(client, *this);
        uploads_manager_ = std::make_unique<filesystem_uploads>(client, *this);
    }

    void filesystem::handle_get(input& in, output& out){
        // In describe mode, return schema without executing
        if(in.describe()) {
            in["path"] = "";
            out = json_t::array();  // Returns array of files or file content
            return;
        }

        // Use centralized path resolution and validation
        std::filesystem::path target_path;
        if(!resolve_and_validate_path(in, out, target_path, "path", true)) {
            return; // Error already set by resolve_and_validate_path
        }

        std::error_code ec;
        auto status = std::filesystem::status(target_path, ec);
        if(ec) {
            out.set_error(ec.message().c_str());
            return;
        }

        // Check if it's a directory or file
        if(std::filesystem::is_directory(status)){
            handle_list(in, out);
        } else if(std::filesystem::is_regular_file(status)){
            handle_download(in, out);
        } else {
            out.set_error(400, "Path is neither a regular file nor a directory");
        }
    }

    void filesystem::handle_list(input& in, output& out){
        // Get parameters from PARAMETERS field (not PAYLOAD)
        json_t& params = in.get_params();
        bool include_hidden = get_value(params, "include_hidden", false);

        // Use centralized path resolution and validation
        std::filesystem::path target_path;
        if(!resolve_and_validate_path(in, out, target_path, "path", true)) {
            return; // Error already set by resolve_and_validate_path
        }

        std::error_code ec;
        if(!std::filesystem::is_directory(target_path, ec)){
            out.set_error(400, "Path is not a directory");
            return;
        }

        // List directory entries as a JSON array
        out = json_t::array();

        for(const auto& entry : std::filesystem::directory_iterator(target_path, ec)){
            std::error_code entry_ec;
            std::string filename = entry.path().filename().string();

            // Skip hidden files if not requested
            if(!include_hidden && !filename.empty() && filename[0] == '.') continue;

            // Get file info
            auto status = entry.status(entry_ec);
            if(entry_ec) continue;

            auto ftime = std::filesystem::last_write_time(entry, entry_ec);
            if(entry_ec) continue;

            auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now()
            );
            auto time_t_val = std::chrono::system_clock::to_time_t(sctp);

            // Get file size
            uint64_t file_size = 0;
            if(std::filesystem::is_regular_file(status)){
                file_size = std::filesystem::file_size(entry, entry_ec);
                if(entry_ec) file_size = 0;
            }

            // Create entry object with all fields using initializer list
            out.payload().push_back({
                {"name", filename},
                {"type", get_file_type(status)},
                {"size", file_size},
                {"mode", format_permissions(status.permissions())},
                {"modified", time_t_val}
            });
        }

        if(ec) {
            out.set_error(ec.message().c_str());
            THINGER_LOG_ERROR("error in handle_list: {}", ec.message());
        }
    }

    void filesystem::handle_download(input& in, output& out){
        std::filesystem::path target_path;
        if(!resolve_and_validate_path(in, out, target_path, "path", true)) {
            return; // Error already set in output
        }

        std::error_code ec;
        if(!std::filesystem::is_regular_file(target_path, ec)){
            out.set_error(400, "Not a regular file");
            return;
        }

        auto file_size = std::filesystem::file_size(target_path, ec);
        if(ec) {
            out.set_error(ec.message().c_str());
            return;
        }

        // Check file size limit
        if(file_size > MAX_INLINE_FILE_SIZE){
            out.set_error(413, "File too large for inline transfer");
            out["size"] = (uint64_t)file_size;
            out["max_size"] = (uint64_t)MAX_INLINE_FILE_SIZE;
            return;
        }

        // Read file content
        std::ifstream file(target_path, std::ios::binary);
        if(!file.is_open()){
            out.set_error("Failed to open file");
            return;
        }

        // Read file into buffer
        std::vector<uint8_t> buffer(file_size);
        file.read(reinterpret_cast<char*>(buffer.data()), file_size);

        if(!file.good()){
            out.set_error("Failed to read file");
            return;
        }

        // Return file content as bytes directly
        json_t& data = out;
        data = json_t::binary({buffer.begin(), buffer.end()});

        THINGER_LOG("inline file transfer: {} ({} bytes)", target_path.filename().string(), file_size);
    }
    
    void filesystem::handle_put(input& in, output& out){
        // In describe mode, return schema without executing
        if(in.describe()) {
            in["path"] = "";
            out["success"] = true;
            out["path"] = "";
            out["type"] = "";
            return;
        }

        // Get parameters from PARAMETERS field
        json_t& params = in.get_params();

        // First check if this is a directory creation (path ends with /)
        // We need to peek at the path to determine this
        std::string path_str = in.path("path", "");
        bool is_directory = !path_str.empty() && path_str.back() == '/';

        // Use centralized path resolution and validation
        // Don't check if path exists yet - we may be creating it
        std::filesystem::path target_path;
        if(!resolve_and_validate_path(in, out, target_path, "path", false)) {
            return; // Error already set by resolve_and_validate_path
        }

        // Check if data is provided in PAYLOAD (bytes directly, not wrapped)
        json_t& payload = in;
        size_t data_size = 0;
        const uint8_t* data = nullptr;

        if(payload.is_binary()){
            // Get the bytes data directly from payload
            auto& binary = payload.get_binary();
            data = binary.data();
            data_size = binary.size();

            // Check size limit for inline transfers
            if(data_size > MAX_INLINE_FILE_SIZE){
                out.set_error(413, "File too large for inline transfer");
                out["size"] = (uint64_t)data_size;
                out["max_size"] = (uint64_t)MAX_INLINE_FILE_SIZE;
                return;
            }
        }

        // Check overwrite policy from parameters - default is true (standard PUT behavior)
        // Client should explicitly set overwrite=false for safe creation
        bool overwrite = params["overwrite"].is_null() ? true : (bool)params["overwrite"];

        std::error_code ec;
        bool file_exists = std::filesystem::exists(target_path, ec);

        if(file_exists){
            if(is_directory){
                if(std::filesystem::is_directory(target_path, ec)){
                    // Directory already exists, this is OK
                    out["success"] = true;
                    out["path"] = target_path.string().c_str();
                    out["type"] = "directory";
                    out["message"] = "Directory already exists";
                    return;
                }
                out.set_error(409, "A file exists at this path");
                return;
            }

            // For files: check if overwrite is allowed
            if(!overwrite){
                out.set_error(409, "File already exists (overwrite=false)");
                return;
            }
        }

        if(is_directory){
            // Create directory
            std::filesystem::create_directories(target_path, ec);
            if(ec) {
                out.set_error(ec.message().c_str());
                THINGER_LOG_ERROR("error creating directory: {}", ec.message());
                return;
            }
            THINGER_LOG("created directory: {}", target_path.string());

            out["success"] = true;
            out["path"] = target_path.string().c_str();
            out["type"] = "directory";
            return;
        }

        // Create parent directories if needed
        std::filesystem::create_directories(target_path.parent_path(), ec);
        if(ec) {
            out.set_error(ec.message().c_str());
            return;
        }

        // Write the file
        std::ofstream file(target_path, std::ios::binary | std::ios::trunc);
        if(!file.is_open()){
            out.set_error("Failed to create file");
            return;
        }

        // Write the data if any
        if(data && data_size > 0){
            file.write(reinterpret_cast<const char*>(data), data_size);

            if(!file.good()){
                out.set_error("Failed to write file");
                file.close();
                // Try to clean up the partial file
                std::filesystem::remove(target_path, ec);
                return;
            }
        }

        file.close();

        // Return success
        out["success"] = true;
        out["path"] = target_path.string().c_str();
        out["type"] = "file";
        out["size"] = (uint64_t)data_size;

        if(data_size > 0){
            if(file_exists){
                THINGER_LOG("updated file: {} ({} bytes)", target_path.filename().string(), data_size);
            } else {
                THINGER_LOG("created file: {} ({} bytes)", target_path.filename().string(), data_size);
            }
        } else {
            if(file_exists){
                THINGER_LOG("truncated file: {} (0 bytes)", target_path.filename().string());
            } else {
                THINGER_LOG("created empty file: {}", target_path.filename().string());
            }
        }
    }
    
    void filesystem::handle_info(input& in, output& out){
        // In describe mode, return schema without executing
        if(in.describe()) {
            in["path"] = "";
            out["name"] = "";
            out["path"] = "";
            out["type"] = "";
            out["size"] = 0;
            out["mode"] = "";
            out["modified"] = 0;
            return;
        }

        std::filesystem::path target_path;
        if(!resolve_and_validate_path(in, out, target_path, "path", true)) {
            return; // Error already set in output
        }

        std::error_code ec;
        auto status = std::filesystem::status(target_path, ec);
        if(ec) {
            out.set_error(ec.message().c_str());
            return;
        }

        out["name"] = target_path.filename().string().c_str();
        out["path"] = target_path.string().c_str();
        out["type"] = get_file_type(status).c_str();

        if(std::filesystem::is_regular_file(status)){
            auto fsize = std::filesystem::file_size(target_path, ec);
            out["size"] = ec ? (uint64_t)0 : (uint64_t)fsize;
        }

        out["mode"] = format_permissions(status.permissions()).c_str();

        auto ftime = std::filesystem::last_write_time(target_path, ec);
        if(!ec) {
            auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now()
            );
            auto time_t_val = std::chrono::system_clock::to_time_t(sctp);
            out["modified"] = (uint64_t)time_t_val;
        }
    }

    void filesystem::handle_delete(input& in, output& out){
        // In describe mode, return schema without executing
        if(in.describe()) {
            in["path"] = "";
            out["success"] = true;
            out["deleted"] = "";
            return;
        }

        // Use centralized path resolution and validation
        std::filesystem::path target_path;
        if(!resolve_and_validate_path(in, out, target_path, "path", true)) {
            return; // Error already set by resolve_and_validate_path
        }

        // Prevent deletion of base path or parent directories
        if(target_path == base_path_ || target_path == base_path_.parent_path()){
            out.set_error(403, "Cannot delete base or parent directory");
            return;
        }

        // Get recursive flag from parameters (sent by server)
        json_t& params = in.get_params();
        bool recursive = get_value(params, "recursive", false);

        std::error_code ec;
        if(std::filesystem::is_directory(target_path, ec)){
            if(recursive){
                std::filesystem::remove_all(target_path, ec);
            } else {
                std::filesystem::remove(target_path, ec);
            }
        } else {
            std::filesystem::remove(target_path, ec);
        }

        if(ec) {
            out.set_error(ec.message().c_str());
            THINGER_LOG_ERROR("error in handle_delete: {}", ec.message());
            return;
        }

        out["success"] = true;
        out["deleted"] = target_path.string().c_str();
    }

    // Removed: handle_mkdir is now integrated into handle_put
    // Use PUT with path ending in '/' to create directories

    void filesystem::handle_move(input& in, output& out){
        // In describe mode, return schema without executing
        if(in.describe()) {
            in["source"] = "";
            in["destination"] = "";
            out["success"] = true;
            out["source"] = "";
            out["destination"] = "";
            return;
        }

        // Get parameters from PARAMETERS field (for overwrite flag)
        json_t& params = in.get_params();

        // Use centralized validation for source path (from URL wildcard)
        std::filesystem::path source_path;
        if(!resolve_and_validate_path(in, out, source_path, "source", true)) {
            return; // Error already set by resolve_and_validate_path
        }

        // Get destination from payload (sent by server in payload["destination"])
        json_t& payload = in.payload();
        std::string destination_str = get_value(payload, "destination", empty::string);
        if(destination_str.empty()){
            out.set_error(400, "Destination path is required");
            return;
        }

        // Resolve and validate destination path
        std::filesystem::path dest_path = resolve_path(destination_str);
        if(!validate_resolved_path(dest_path)) {
            out.set_error(403, "Access denied: destination path outside allowed directory");
            return;
        }

        bool overwrite = get_value(params, "overwrite", false);

        std::error_code ec;
        if(std::filesystem::exists(dest_path, ec) && !overwrite){
            out.set_error(409, "Destination already exists");
            return;
        }

        std::filesystem::rename(source_path, dest_path, ec);
        if(ec) {
            out.set_error(ec.message().c_str());
            THINGER_LOG_ERROR("error in handle_move: {}", ec.message());
            return;
        }

        out["success"] = true;
        out["source"] = source_path.string().c_str();
        out["destination"] = dest_path.string().c_str();
    }

    bool filesystem::is_path_safe(const std::string& path) const {
        // Basic safety check - no path traversal
        if(path.find("..") != std::string::npos){
            return false;
        }
        
        // Require absolute paths
        if(path.empty() || path[0] != '/') {
            return false;
        }
        
        return true;
    }
    
    bool filesystem::is_within_base_path(const std::filesystem::path& path) const {
        // If no base_path restriction, allow all
        if(base_path_.empty()) return true;
        
        // Check if path is within base_path
        auto canonical_base = std::filesystem::canonical(base_path_);
        auto canonical_path = std::filesystem::weakly_canonical(path);
        
        // Check if path starts with base_path
        auto [base_end, nothing] = std::mismatch(canonical_base.begin(), canonical_base.end(), 
                                                  canonical_path.begin(), canonical_path.end());
        return base_end == canonical_base.end();
    }

    std::string filesystem::normalize_path(const std::string& path) const {
        std::filesystem::path p(path);
        return p.lexically_normal().string();
    }

    std::string filesystem::get_file_type(const std::filesystem::file_status& status) const {
        if(std::filesystem::is_regular_file(status)) return "file";
        if(std::filesystem::is_directory(status)) return "directory";
        if(std::filesystem::is_symlink(status)) return "symlink";
        if(std::filesystem::is_block_file(status)) return "block";
        if(std::filesystem::is_character_file(status)) return "character";
        if(std::filesystem::is_fifo(status)) return "fifo";
        if(std::filesystem::is_socket(status)) return "socket";
        return "unknown";
    }

    std::string filesystem::format_permissions(const std::filesystem::perms& p) const {
        std::stringstream ss;
        
        auto check = [&](std::filesystem::perms perm, char c) {
            ss << ((p & perm) != std::filesystem::perms::none ? c : '-');
        };
        
        // Owner permissions
        check(std::filesystem::perms::owner_read, 'r');
        check(std::filesystem::perms::owner_write, 'w');
        check(std::filesystem::perms::owner_exec, 'x');
        
        // Group permissions
        check(std::filesystem::perms::group_read, 'r');
        check(std::filesystem::perms::group_write, 'w');
        check(std::filesystem::perms::group_exec, 'x');
        
        // Others permissions
        check(std::filesystem::perms::others_read, 'r');
        check(std::filesystem::perms::others_write, 'w');
        check(std::filesystem::perms::others_exec, 'x');
        
        return ss.str();
    }

    // Filesystem downloads stream manager implementation
    filesystem_downloads::filesystem_downloads(client& client, filesystem& fs)
        : stream_manager(client, "$fs/download/:session")
        , fs_(fs)
    {
        THINGER_LOG("initializing filesystem downloads");
    }

    std::shared_ptr<stream_session>
    filesystem_downloads::create_session(client& client, uint16_t stream_id, std::string session, json_t& parameters) {
        std::string path = get_value(parameters, "path", empty::string);
        if(path.empty()) {
            THINGER_LOG_ERROR("download: missing path parameter");
            return nullptr;
        }

        // Use filesystem's centralized path resolution and validation
        std::filesystem::path file_path = fs_.resolve_path(path);

        // Validate the resolved path
        if(!fs_.validate_resolved_path(file_path)) {
            THINGER_LOG_ERROR("download: access denied - invalid or restricted path");
            return nullptr;
        }

        // Build transfer configuration from parameters
        transfer_config config;
        config.stream_id = stream_id;
        config.session_name = session;
        config.file_path = file_path;

        // Device decides chunk_size for download (how fast it can read from disk)
        config.chunk_size = DEFAULT_CHUNK_SIZE;

        // Get window size from parameters (in bytes), use default if not specified
        // Server controls window size (how much it can buffer)
        config.window_size = get_value(parameters, "window_size", DEFAULT_WINDOW_SIZE);

        // Get bandwidth limit from parameters (in Mbps), use default if not specified
        auto& bandwidth_param = parameters["max_bandwidth_mbps"];
        if(!bandwidth_param.is_null()) {
            config.max_bandwidth_mbps = bandwidth_param;
        }

        if(config.max_bandwidth_mbps > 0) {
            THINGER_LOG("download: bandwidth limited to {} Mbps", config.max_bandwidth_mbps);
        }

        return std::make_shared<file_download_session>(client, config);
    }
    
    // Filesystem uploads stream manager implementation
    filesystem_uploads::filesystem_uploads(client& client, filesystem& fs)
        : stream_manager(client, "$fs/upload/:session")
        , fs_(fs)
    {
        THINGER_LOG("initializing filesystem uploads");
    }

    std::shared_ptr<stream_session>
    filesystem_uploads::create_session(client& client, uint16_t stream_id, std::string session, json_t& parameters) {
        std::string path = get_value(parameters, "path", empty::string);
        if(path.empty()) {
            THINGER_LOG_ERROR("upload: missing path parameter");
            return nullptr;
        }

        // Get expected file size from parameters (required)
        size_t expected_size = get_value(parameters, "size", static_cast<size_t>(0));
        if(expected_size == 0) {
            THINGER_LOG_ERROR("upload: missing or invalid size parameter");
            return nullptr;
        }

        // Use filesystem's centralized path resolution and validation
        std::filesystem::path file_path = fs_.resolve_path(path);

        // Validate the resolved path
        if(!fs_.validate_resolved_path(file_path)) {
            THINGER_LOG_ERROR("upload: access denied - invalid or restricted path");
            return nullptr;
        }

        // Check available disk space before starting upload
        std::error_code ec;
        std::filesystem::space_info space = std::filesystem::space(file_path.parent_path(), ec);
        if(ec) {
            THINGER_LOG_ERROR("upload: failed to check disk space - {}", ec.message());
            return nullptr;
        }
        if(space.available < expected_size) {
            THINGER_LOG_ERROR("upload: insufficient disk space - need {} bytes, available {} bytes",
                             expected_size, space.available);
            return nullptr;
        }

        // Build transfer configuration from parameters
        transfer_config config;
        config.stream_id = stream_id;
        config.session_name = session;
        config.file_path = file_path;
        config.expected_size = expected_size;

        // Device decides chunk_size for upload (how fast it can write to disk)
        config.chunk_size = DEFAULT_CHUNK_SIZE;

        // Get bandwidth limit from parameters (in Mbps), use default if not specified
        auto& bandwidth_param = parameters["max_bandwidth_mbps"];
        if(!bandwidth_param.is_null()) {
            config.max_bandwidth_mbps = bandwidth_param;
        }

        if(config.max_bandwidth_mbps > 0) {
            THINGER_LOG("upload: bandwidth limited to {} Mbps", config.max_bandwidth_mbps);
        }

        return std::make_shared<file_upload_session>(client, config);
    }
    
    std::filesystem::path filesystem::resolve_path(const std::string& input_path) const {
        // Simple path resolution using std::filesystem capabilities
        // - Empty path means the root (base_path if configured, current dir otherwise)
        // - All paths are treated as relative to our virtual root (base_path)
        
        std::filesystem::path resolved;
        
        if(input_path.empty()) {
            // Empty path means root directory
            resolved = base_path_.empty() ? std::filesystem::current_path() : base_path_;
            THINGER_LOG("resolve_path: empty input -> '{}'", resolved.string());
            return resolved;
        }
        
        // Convert string to path and let filesystem handle it
        std::filesystem::path path(input_path);
        
        if(!base_path_.empty()) {
            // With base_path configured, everything is relative to it
            // Use lexically_relative to get the relative part if it's absolute
            if(path.is_absolute()) {
                // For absolute paths, make them relative to root "/"
                path = path.lexically_relative("/");
            }
            // Append to base_path
            resolved = (base_path_ / path).lexically_normal();
        } else {
            // No base_path restriction
            if(path.is_relative()) {
                // Make relative paths absolute by prepending "/"
                resolved = ("/" / path).lexically_normal();
            } else {
                // Already absolute
                resolved = path.lexically_normal();
            }
        }

        THINGER_LOG("resolve_path: '{}' -> '{}'", input_path, resolved.string());
        return resolved;
    }
    
    bool filesystem::validate_resolved_path(const std::filesystem::path& path) const {
        // Check for path traversal attempts
        if(path.string().find("..") != std::string::npos) {
            return false;
        }
        
        // If base_path is set, verify the path is within it
        if(!base_path_.empty()) {
            if(!is_within_base_path(path)) {
                return false;
            }
        }
        
        return true;
    }
    
    bool filesystem::resolve_and_validate_path(input& in, output& out, std::filesystem::path& resolved_path, 
                                                const char* wildcard_name, bool check_exists) {
        // Try to get path from URL wildcard first
        std::string path_str;
        if(wildcard_name) {
            path_str = in.path(wildcard_name, "");
        }
        
        // Fall back to payload if no wildcard or empty
        if(path_str.empty()){
            json_t& path_json = in[wildcard_name ? wildcard_name : "path"];
            if(!path_json.is_null() && path_json.is_string()){
                path_str = path_json.get<std::string>();
            }
        }
        
        // Resolve path using our virtual root mapping
        // Empty path is handled by resolve_path (maps to base_path or current dir)
        resolved_path = resolve_path(path_str);
        
        // Validate the resolved path
        if(!validate_resolved_path(resolved_path)) {
            out.set_error(403, "Access denied: path outside allowed directory");
            return false;
        }
        
        // Optionally check if path exists
        if(check_exists && !std::filesystem::exists(resolved_path)){
            out.set_error(404, "Path does not exist");
            return false;
        }
        
        return true;
    }

}