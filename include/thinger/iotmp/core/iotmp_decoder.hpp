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

#include "pson.h"
#include "iotmp_message.hpp"

namespace thinger::iotmp{

    class iotmp_decoder : public protoson::pson_decoder{
    public:
        bool decode_field(message::wire_type& type, uint32_t& field_number)
        {
            uint32_t temp=0;
            if(!pb_decode_varint32(temp)) return false;
            type = (message::wire_type)(temp & 0x07);
            field_number = temp >> 3;
            return true;
        }
        bool decode(iotmp_message&  message, size_t size){
            size_t start_read = bytes_read();
            while(size-(bytes_read()-start_read)>0) {
                message::wire_type wire_type;
                uint32_t field_number;
                if(!decode_field(wire_type, field_number)) return false;
                switch (wire_type) {
                    case message::wire_type::varint: {
                        uint32_t value = 0;
                        if(!pb_decode_varint32(value)) return false;
                        message[field_number] = value;
                        break;
                    }
                    case message::wire_type::pson:{
                        if(!protoson::pson_decoder::decode(message[field_number])) return false;
                        break;
                    }
                    default:
                        return false;
                }
            }
            return true;
        }
    };

    class iotmp_read_decoder : public iotmp_decoder{
    public:
        iotmp_read_decoder(iotmp_io& io) : io_(io)
        {}

    protected:
        virtual bool read(void* buffer, size_t size){
            return io_.read((char*)buffer, size) && protoson::pson_decoder::read(buffer, size);
        }

    private:
        iotmp_io& io_;
    };

    class iotmp_memory_decoder : public iotmp_decoder{

    public:
        iotmp_memory_decoder(uint8_t* buffer, size_t size) : buffer_(buffer), size_(size){}

    protected:
        virtual bool read(void* buffer, size_t size){
            if(read_+size<=size_){
                memcpy(buffer, buffer_ + read_, size);
                return protoson::pson_decoder::read(buffer, size);
            }else{
                return false;
            }
        }

    private:
        uint8_t* buffer_;
        size_t size_;
    };

}

#endif