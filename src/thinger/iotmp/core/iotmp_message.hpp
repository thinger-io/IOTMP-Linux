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
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#ifndef THINGER_IOTMP_MESSAGE_HPP
#define THINGER_IOTMP_MESSAGE_HPP

#include "iotmp_types.hpp"
#include <unordered_map>

namespace thinger::iotmp{


    namespace message{

        enum wire_type{
            varint                  = 0x00,  // For numeric fields (stream_id, etc.)
            pson_v1                 = 0x01,  // Legacy PSON (protoson library)
            pson_v2                 = 0x02,  // New PSON (nlohmann::json + PSON wire format)
            // Future protocol extensions:
            // msgpack              = 0x03,
            // cbor                 = 0x04,
            // protobuf             = 0x05,
            // 0x06-0x07 reserved
        };

        enum type {
            RESERVED                = 0x00,
            OK                      = 0x01,
            ERROR                   = 0x02,
            CONNECT                 = 0x03,
            DISCONNECT              = 0x04,
            KEEP_ALIVE              = 0x05,
            RUN                     = 0x06,
            DESCRIBE                = 0x07,
            START_STREAM            = 0x08,
            STOP_STREAM             = 0x09,
            STREAM_DATA             = 0x0a
        };

        enum field {
            STREAM_ID               = 0X01,
            PARAMETERS              = 0x02,
            PAYLOAD                 = 0x03,
            RESOURCE                = 0x04,
        };

        namespace stream{
            enum class parameters{
                RESOURCE_INPUT      = 0x01,
                RESOURCE_OUTPUT     = 0x02
            };
        }

        namespace connect{
            // Connect message uses standard fields:
            // - PARAMETERS (0x02): connection parameters (optional)
            // - PAYLOAD (0x03): auth data (format depends on "at" parameter)
            
            // Parameter keys for CONNECT message
            constexpr const char* PROTOCOL_VERSION = "pv";  // Protocol version (0=legacy PSON, 1=new PSON)
            constexpr const char* KEEP_ALIVE = "ka";         // Keep-alive interval in seconds
            constexpr const char* AUTH_TYPE = "at";          // Authentication type (default: 0 = CREDENTIALS)
            
            // Future parameter keys:
            constexpr const char* CLIENT_TYPE = "ct";        // Client type/platform
            constexpr const char* FIRMWARE = "fw";           // Firmware version
            
            // Authentication types
            enum class auth_type : uint8_t {
                CREDENTIALS = 0,     // Default: [username, device, password] in PAYLOAD
                // Future auth types:
                // AUTO_PROVISION = 1,  // Auto-provisioning with provision key
                // TOKEN = 2,           // JWT/Bearer token
                // API_KEY = 3,         // API key authentication
            };
        }
    }

    namespace server{
        enum run{
            READ_DEVICE_PROPERTY        = 0X01,
            SET_DEVICE_PROPERTY         = 0X02,
            CALL_DEVICE                 = 0X03,
            CALL_ENDPOINT               = 0X04,
            WRITE_BUCKET                = 0X05,
            LOCK_SYNC                   = 0X06,
            UNLOCK_SYNC                 = 0X07,
            SUBSCRIBE_EVENT             = 0x08
        };
    }


    class iotmp_message{

    public:
        /**
         * Initialize a default empty message
         */
        explicit iotmp_message(message::type type) : message_type_(type)
        {

        }

        /**
        * Initialize a default empty message
        */
        explicit iotmp_message(uint16_t stream_id, message::type type) : message_type_(type)
        {
            set_stream_id(stream_id);
        }

        ~iotmp_message() {

        };

    private:
        /// store message type
        message::type message_type_;

        /// message fields
        std::unordered_map<uint8_t, json_t> fields_;

    public:

        message::type  get_message_type() const{
            return message_type_;
        }

        void set_message_type(message::type type){
            message_type_ = type;
        }

        const char* message_type() const{
            switch(message_type_){
                case message::type::RESERVED:
                    return "RESERVED";
                case message::type::OK:
                    return "OK";
                case message::type::ERROR:
                    return "ERROR";
                case message::type::KEEP_ALIVE:
                    return "KEEP_ALIVE";
                case message::type::RUN:
                    return "RUN_RESOURCE";
                case message::type::DESCRIBE:
                    return "DESCRIBE";
                case message::type::START_STREAM:
                    return "START_STREAM";
                case message::type::STOP_STREAM:
                    return "STOP_STREAM";
                case message::type::CONNECT:
                    return "CONNECT";
                case message::type::STREAM_DATA:
                    return "STREAM_DATA";
                default:
                    return "UNKNOWN";
            }
        }

    public:

        uint16_t get_stream_id(){
            auto it = fields_.find(message::field::STREAM_ID);
            if(it != fields_.end() && it->second.is_number()) {
                return it->second.get<uint16_t>();
            }
            return 0;
        }

        void set_stream_id(uint16_t stream_id) {
            fields_[message::field::STREAM_ID] = stream_id;
        }

        void set_random_stream_id(){
            // TODO, random seed
            fields_[message::field::STREAM_ID] = (uint16_t) rand();
        }

        std::unordered_map<uint8_t, json_t>& get_fields(){
            return fields_;
        }

        bool has_field(uint8_t flag_id) const{
            return fields_.contains(flag_id);
        }

        json_t& operator[](uint8_t field){
            return fields_[field];
        }

        bool remove_field(uint8_t field_id){
            return fields_.erase(field_id);
        }

        void set_field(uint8_t flag_id, const json_t& data){
            fields_[flag_id] = data;
        }

        // Convenience methods for accessing common fields
        json_t& params(){
            return fields_[message::field::PARAMETERS];
        }

        const json_t& params() const{
            auto it = fields_.find(message::field::PARAMETERS);
            static const json_t empty_json;
            return it != fields_.end() ? it->second : empty_json;
        }

        json_t& payload(){
            return fields_[message::field::PAYLOAD];
        }

        const json_t& payload() const{
            auto it = fields_.find(message::field::PAYLOAD);
            static const json_t empty_json;
            return it != fields_.end() ? it->second : empty_json;
        }

        bool has_params() const{
            return has_field(message::field::PARAMETERS);
        }

        bool has_payload() const{
            return has_field(message::field::PAYLOAD);
        }
    };

}


#endif