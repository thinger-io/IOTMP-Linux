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

#ifndef THINGER_CLIENT_ENCODER_HPP
#define THINGER_CLIENT_ENCODER_HPP

#include "iotmp_message.hpp"
#include "iotmp_io.hpp"

namespace thinger::iotmp{

    class iotmp_encoder : public protoson::pson_encoder{

    protected:
        virtual bool write(const void *buffer, size_t size){
            return protoson::pson_encoder::write(buffer, size);
        }

        void encode_field(message::wire_type wire_type, uint32_t field_number){
            uint64_t tag = ((uint64_t)field_number << 3) | wire_type;
            pb_encode_varint(tag);
        }

    public:

        void encode(iotmp_message& message){
            for(auto it = message.get_fields().begin(); it.valid(); it.next()){
                pson& value = it.item().right;
                if(value.is_number()){
                    pb_encode_varint(it.item().left, value);
                }else{
                    encode_field(message::wire_type::pson, it.item().left);
                    protoson::pson_encoder::encode(value);
                }
            }
        }
    };

    class itomp_write_encoder : public iotmp_encoder{
    public:
        itomp_write_encoder(iotmp_io& io) : io_(io)
        {}

    protected:
        virtual bool write(const void *buffer, size_t size){
            return io_.write((const char*)buffer, size) && protoson::pson_encoder::write(buffer, size);
        }

    private:
        iotmp_io& io_;
    };

    class iotmp_memory_encoder : public iotmp_encoder{

    public:
        iotmp_memory_encoder(uint8_t* buffer, size_t size) : buffer_(buffer), size_(size){}

    protected:
        virtual bool write(const void *buffer, size_t size){
            if(written_+size < size_){
                memcpy(buffer_ + written_, buffer, size);
                return protoson::pson_encoder::write(buffer, size);
            }
            return false;
        }

    private:
        uint8_t* buffer_;
        size_t size_;
    };

}

#endif