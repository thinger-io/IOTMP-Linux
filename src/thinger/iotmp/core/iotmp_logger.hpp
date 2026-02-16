// The MIT License (MIT)
//
// Copyright (c) 2017 THINK BIG LABS SL
// Author: alvarolb@gmail.com (Alvaro Luis Bustamante)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR IN CONNECTION WITH
// THE SOFTWARE.

#ifndef THINGER_IOTMP_LOGGER_HPP
#define THINGER_IOTMP_LOGGER_HPP

#include "iotmp_message.hpp"
#include <sstream>
#include <iomanip>
#include <algorithm>

#ifndef THINGER_LOG_TAG
#define THINGER_LOG_TAG(tag, ...) ((void)0)
#endif

namespace thinger::iotmp {

    /**
     * Helper class for formatting and logging IOTMP messages
     * Provides methods to dump message contents in a readable format with colors and symbols
     */
    class message_logger {
    public:

        // ANSI color codes for terminal output
        static constexpr const char* RESET = "\033[0m";
        static constexpr const char* BOLD = "\033[1m";
        static constexpr const char* DIM = "\033[2m";

        // Message type colors
        static constexpr const char* TYPE_COLOR = "\033[1;36m";     // Cyan bold
        static constexpr const char* STREAM_COLOR = "\033[1;35m";   // Magenta bold
        static constexpr const char* RESOURCE_COLOR = "\033[1;33m"; // Yellow bold
        static constexpr const char* PARAMS_COLOR = "\033[0;34m";   // Blue
        static constexpr const char* PAYLOAD_COLOR = "\033[0;32m";  // Green
        static constexpr const char* BINARY_COLOR = "\033[0;90m";   // Gray

        // Unicode symbols
        static constexpr const char* ARROW_IN = "←";
        static constexpr const char* ARROW_OUT = "→";
        static constexpr const char* DOT = "•";

        /**
         * Dump a message to a string with full details
         * Format: [MSG_TYPE] sid=123 • resource="..." • params={...} • payload={...}
         * @param msg The message to dump
         * @param include_binary Whether to include binary payload info (default: false)
         * @param use_colors Whether to use ANSI colors (default: true)
         * @param is_incoming Whether this is an incoming message (for arrow direction)
         * @return Formatted string representation of the message
         */
        static std::string dump(iotmp_message& msg, bool include_binary = false, bool use_colors = true, bool is_incoming = true) {
            std::ostringstream ss;

            // Direction arrow with color
            if (use_colors) ss << DIM;
            ss << (is_incoming ? ARROW_IN : ARROW_OUT);
            if (use_colors) ss << RESET;

            // Message type (always present) with color - lowercase and fixed width
            if (use_colors) ss << " " << TYPE_COLOR;
            std::string msg_type = msg.message_type();
            std::transform(msg_type.begin(), msg_type.end(), msg_type.begin(), ::tolower);
            ss << std::setw(14) << std::left << msg_type;
            if (use_colors) ss << RESET;

            // Stream ID with color
            if (msg.has_field(message::field::STREAM_ID)) {
                if (use_colors) ss << " " << DIM << DOT << RESET << " " << STREAM_COLOR;
                else ss << " " << DOT << " ";
                ss << "sid:" << std::setw(5) << std::right << msg.get_stream_id();
                if (use_colors) ss << RESET;
            }

            // Resource with color
            if (msg.has_field(message::field::RESOURCE)) {
                auto& resource = msg[message::field::RESOURCE];
                if (use_colors) ss << " " << DIM << DOT << RESET << " " << RESOURCE_COLOR;
                else ss << " " << DOT << " ";

                if (resource.is_string()) {
                    ss << resource.get<std::string>();
                } else if (resource.is_number_unsigned()) {
                    ss << "#" << resource.get<uint32_t>();
                } else if (resource.is_array()) {
                    ss << resource.dump();
                }
                if (use_colors) ss << RESET;
            }

            // Parameters with color
            if (msg.has_field(message::field::PARAMETERS)) {
                auto& params = msg[message::field::PARAMETERS];
                if (use_colors) ss << " " << DIM << DOT << RESET << " " << PARAMS_COLOR;
                else ss << " " << DOT << " ";
                ss << format_json_value(params, 150);
                if (use_colors) ss << RESET;
            }

            // Payload with color
            if (msg.has_field(message::field::PAYLOAD)) {
                auto& payload = msg[message::field::PAYLOAD];
                if (payload.is_binary()) {
                    if (include_binary) {
                        if (use_colors) ss << " " << DIM << DOT << RESET << " " << BINARY_COLOR;
                        else ss << " " << DOT << " ";
                        ss << "⟨" << format_size(payload.get_binary().size()) << "⟩";
                        if (use_colors) ss << RESET;
                    }
                } else {
                    if (use_colors) ss << " " << DIM << DOT << RESET << " " << PAYLOAD_COLOR;
                    else ss << " " << DOT << " ";
                    ss << format_json_value(payload, 150);
                    if (use_colors) ss << RESET;
                }
            }

            return ss.str();
        }

        /**
         * Dump a message with compact format (no binary payloads)
         * @param msg The message to dump
         * @param is_incoming Whether this is an incoming message
         * @return Compact string representation
         */
        static std::string dump_compact(iotmp_message& msg, bool is_incoming = true) {
            return dump(msg, false, true, is_incoming);
        }

        /**
         * Dump a message with full details including binary info
         * @param msg The message to dump
         * @param is_incoming Whether this is an incoming message
         * @return Full string representation
         */
        static std::string dump_full(iotmp_message& msg, bool is_incoming = true) {
            return dump(msg, true, true, is_incoming);
        }

        /**
         * Log an incoming message
         * @param msg The message to log
         * @param include_binary Whether to include binary payload info (default: false)
         */
        static void log_incoming(iotmp_message& msg, bool include_binary = false) {
            THINGER_LOG_TAG("iotmp", "{}", dump(msg, include_binary, true, true));
        }

        /**
         * Log an outgoing message
         * @param msg The message to log
         * @param include_binary Whether to include binary payload info (default: false)
         */
        static void log_outgoing(iotmp_message& msg, bool include_binary = false) {
            THINGER_LOG_TAG("iotmp", "{}", dump(msg, include_binary, true, false));
        }

    private:
        /**
         * Format a JSON value for logging (truncate if too long)
         * @param value The JSON value to format
         * @param max_length Maximum length before truncation (default: 200)
         * @return Formatted string
         */
        static std::string format_json_value(const json_t& value, size_t max_length = 200) {
            std::string result = value.dump();

            // Truncate if too long
            if (result.length() > max_length) {
                result = result.substr(0, max_length) + "…";
            }

            return result;
        }

        /**
         * Format byte size in human-readable format
         * @param bytes Number of bytes
         * @return Formatted string (e.g., "1.5KB", "2.3MB")
         */
        static std::string format_size(size_t bytes) {
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(1);

            if (bytes < 1024) {
                ss << bytes << "B";
            } else if (bytes < 1024 * 1024) {
                ss << (bytes / 1024.0) << "KB";
            } else if (bytes < 1024 * 1024 * 1024) {
                ss << (bytes / (1024.0 * 1024.0)) << "MB";
            } else {
                ss << (bytes / (1024.0 * 1024.0 * 1024.0)) << "GB";
            }

            return ss.str();
        }
    };

} // namespace thinger::iotmp

#endif // THINGER_IOTMP_MESSAGE_LOGGER_HPP