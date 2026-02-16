#ifndef THINGER_RESULT_HPP
#define THINGER_RESULT_HPP

#include "iotmp_message.hpp"
#include <functional>
#include <string>

namespace thinger::iotmp{

    /**
     * Result wrapper for stream operations
     * Provides convenient constructors for common cases while internally using iotmp_message
     * This ensures proper separation of PARAMETERS and PAYLOAD fields
     */
    class exec_result {
    public:
        /**
         * Simple success/failure result
         * @param success true for OK, false for ERROR
         */
        exec_result(bool success)
            : message_(success ? message::type::OK : message::type::ERROR)
        {
        }

        /**
         * Result with error message
         * @param success true for OK, false for ERROR
         * @param error_message Error message (only used if success=false)
         */
        exec_result(bool success, const std::string& error_message)
            : message_(success ? message::type::OK : message::type::ERROR)
        {
            if(!success && !error_message.empty()) {
                message_.payload()["message"] = error_message;
            }
        }

        /**
         * Custom message result (for advanced cases with PARAMETERS, etc.)
         * @param msg Complete iotmp_message
         */
        exec_result(iotmp_message&& msg)
            : message_(std::move(msg))
        {
        }

        /**
         * Get the underlying iotmp_message
         */
        iotmp_message& get_message() {
            return message_;
        }

        const iotmp_message& get_message() const {
            return message_;
        }

        /**
         * Check if result is successful (message type is OK)
         */
        explicit operator bool() const {
            return message_.get_message_type() == message::type::OK;
        }

        /**
         * Move the message out
         */
        iotmp_message&& take_message() {
            return std::move(message_);
        }

    private:
        iotmp_message message_;
    };

    typedef std::function<void(exec_result&&)> result_handler;

}

#endif