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

#ifndef THINGER_CLIENT_MESSAGE_HPP
#define THINGER_CLIENT_MESSAGE_HPP

#include "pson.h"
#include "pson_to_json.hpp"
#include "thinger_map.hpp"
#include <type_traits>

namespace thinger::iotmp{


    template <typename E>
    constexpr auto to_underlying(E e) noexcept
    {
        return static_cast<std::underlying_type_t<E>>(e);
    }

    namespace message{

        enum wire_type{
            varint                  = 0x00,
            pson                    = 0x01,
            json                    = 0x02,
            messagepack             = 0x03,
            bson                    = 0x04,
            cbor                    = 0x05,
            ubjson                  = 0x06
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

        enum common {
            STREAM_ID               = 0X01
        };

        namespace stream{
            enum  stream{
                PARAMETERS          = 0x02,
                PAYLOAD             = 0x03
            };

            namespace parameters{
                enum parameters{
                    RESOURCE_INPUT      = 0x01,
                    RESOURCE_OUTPUT     = 0x02
                };
            }
        }

        namespace run{
            enum run {
                PARAMETERS           = 0X02,
                PAYLOAD              = 0X03,
                RESOURCE             = 0X04
            };
        }

        namespace connect{
            enum connect {
                PARAMETERS              = 0x02,
                CREDENTIALS             = 0X03,
                KEEP_ALIVE              = 0X04,
                ENCODING                = 0X05
            };
        }

        namespace start_stream{

            enum start_stream {
                PARAMETERS              = 0X02,
                RESOURCE                = 0x03,
                SCOPE                   = 0x04
            };
            namespace scope{
                enum scope{
                    MQTT_SUBSCRIBE      = 0x01,
                    MQTT_PUBLISH        = 0x02,
                    SERVER_EVENT        = 0X03,
                };
            }
        }

        namespace describe{
            enum describe {
                PARAMETERS              = 0x02,
                RESOURCE                = 0x03
            };
        }

        namespace ok{
            enum ok{
                PARAMETERS              = 0X02,
                PAYLOAD                 = 0X03
            };
        }

        namespace error{
            enum ok{
                PARAMETERS              = 0X02,
                PAYLOAD                 = 0X03
            };
        }

        namespace disconnect{
            enum disconnect {
                REASON                  = 0x02,
                PAYLOAD                 = 0X03
            };
        }
    }

    namespace server{
        enum class run : uint8_t{
            NONE                        = 0X00,
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
        thinger_map<uint8_t, pson> fields_;

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

#ifdef ARDUINO
        String dump(bool input) const{
            String result;

            result += String("[") + message_type() + "] ";
            for(auto it = fields_.begin(); it.valid(); it.next()){
                pson& value = it.item().right;
                result += String("(") + it.item().left + ":";
                json_encoder encoder(result);
                encoder.encode(value);
                result += ") ";
            }
            return result;
        }
#else
        std::string dump(bool input){
            std::stringstream result;

            result << "(" << message_type() << ") ";
            for(auto it = fields_.begin(); it.valid(); it.next()){
                pson& value = it.item().right;
                result << "(" << std::to_string(it.item().left) << ":";
                json_encoder encoder(result);
                encoder.encode(value);
                result << ") ";
            }

            return result.str();
        }
        
#endif


    public:

        uint16_t get_stream_id(){
            return fields_[message::common::STREAM_ID];
        }

        void set_stream_id(uint16_t stream_id) {
            fields_[message::common::STREAM_ID] = stream_id;
        }

        void set_random_stream_id(){
            // TODO, random seed
            fields_[message::common::STREAM_ID] = (uint16_t) rand();
        }

        thinger_map<uint8_t, pson>& get_fields(){
            return fields_;
        }

        bool has_field(uint8_t flag_id){
            return fields_.contains(flag_id);
        }

        protoson::pson& operator[](uint8_t field){
            return fields_[field];
        }

        bool remove_field(uint8_t field_id){
            return fields_.erase(field_id);
        }

        void set_field(uint8_t flag_id, protoson::pson& data){
            protoson::pson::swap(data, fields_[flag_id]);
        }
    };

}


#endif