// The MIT License (MIT)
//
// Copyright (c) 2017 THINK BIG LABS S.L.
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

#ifndef THINGER_CLIENT_DECODER_HPP
#define THINGER_CLIENT_DECODER_HPP

#include "iotmp_message.hpp"
#include "iotmp_adapters.hpp"
#include "pson_decoder.hpp"

namespace thinger::iotmp {

    // IOTMP decoder - uses pson_decoder for pson_v2 wire type
    template<class Reader>
    class iotmp_decoder {
    public:
        template<class... Args>
        explicit iotmp_decoder(Args&&... args) : reader_(std::forward<Args>(args)...) {}

        [[nodiscard]] size_t bytes_read() const { return reader_.bytes_read(); }

        bool decode_field(message::wire_type& type, uint32_t& field_number) {
            uint32_t temp = 0;
            if(!pb_decode_varint(temp)) return false;
            type = static_cast<message::wire_type>(temp & 0x07);
            field_number = temp >> 3;
            return true;
        }

        template<typename T>
        bool pb_decode_varint(T& value) {
            uint32_t temp = 0;
            uint8_t byte;
            uint8_t bit_pos = 0;

            do {
                if(!read_byte(&byte) || bit_pos >= 32) {
                    return false;
                }
                temp |= static_cast<uint32_t>(byte & 0x7F) << bit_pos;
                bit_pos += 7;
            } while(byte >= 0x80);

            value = static_cast<T>(temp);
            return true;
        }

        bool decode(iotmp_message& message, size_t size) {
            size_t start_read = reader_.bytes_read();

            while(size - (reader_.bytes_read() - start_read) > 0) {
                message::wire_type wire_type;
                uint32_t field_number;

                if(!decode_field(wire_type, field_number)) return false;

                switch(wire_type) {
                    case message::wire_type::varint: {
                        uint32_t value = 0;
                        if(!pb_decode_varint(value)) return false;
                        message[field_number] = value;
                        break;
                    }
                    case message::wire_type::pson_v2: {
                        if(!decode_pson_value(message[field_number])) return false;
                        break;
                    }
                    default:
                        // Reject unknown wire types (including legacy pson_v1)
                        return false;
                }
            }
            return true;
        }

    private:
        Reader reader_;

        bool read_byte(uint8_t* byte) {
            return reader_.read(byte);
        }

        bool decode_pson_value(nlohmann::json& value) {
            pson_decoder<Reader> decoder(reader_);
            return decoder.decode(value);
        }
    };

    // Convenient type alias
    using iotmp_memory_decoder = iotmp_decoder<memory_reader>;

}

#endif