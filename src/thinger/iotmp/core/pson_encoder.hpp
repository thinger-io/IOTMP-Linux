#ifndef PSON_ENCODER_HPP
#define PSON_ENCODER_HPP

#include <nlohmann/json.hpp>
#include "pson_types.hpp"

namespace thinger::iotmp {

    // PSON v2 encoder - template that writes using any Writer type
    template<class Writer>
    class pson_encoder {
    public:
        explicit pson_encoder(Writer& writer) : writer_(writer) {}

        [[nodiscard]] size_t bytes_written() const { return writer_.bytes_written(); }

        bool pb_encode_tag_fixed(pson_wire_type wire_type, uint8_t value) {
            const uint8_t tag = (static_cast<uint8_t>(wire_type) << 5) | value;
            return write(&tag);
        }

        bool pb_encode_tag(pson_wire_type wire_type, uint64_t value) {
            if(value < 0x1f) {
                return pb_encode_tag_fixed(wire_type, value);
            }
            return pb_encode_tag_fixed(wire_type, 0x1f) && pb_write_varint(value);
        }

        bool pb_write_varint(uint64_t value) {
            do {
                uint8_t byte = (value & 0x7F);
                value >>= 7;
                if(value > 0) byte |= 0x80;
                if(!write(&byte)) return false;
            } while(value > 0);
            return true;
        }

        bool pb_encode_string(const char* str) {
            size_t size = strlen(str);
            return pb_encode_tag(pson_wire_type::string_t, size) && write(str, size);
        }

        bool pb_encode_bytes(const void* data, size_t size) {
            return pb_encode_tag(pson_wire_type::bytes_t, size) && write(data, size);
        }

        bool pb_encode_float(float value) {
            return pb_encode_tag_fixed(pson_wire_type::floating_t, 0) && write(&value, sizeof(float));
        }

        bool pb_encode_double(double value) {
            return pb_encode_tag_fixed(pson_wire_type::floating_t, 1) && write(&value, sizeof(double));
        }

        bool encode_object(const nlohmann::json& object) {
            if(!pb_encode_tag(pson_wire_type::map_t, object.size())) return false;

            for(const auto& [key, value] : object.items()) {
                if(!pb_encode_string(key.c_str())) return false;
                if(!encode(value)) return false;
            }
            return true;
        }

        bool encode_array(const nlohmann::json& array) {
            if(!pb_encode_tag(pson_wire_type::array_t, array.size())) return false;

            for(const auto& element : array) {
                if(!encode(element)) return false;
            }
            return true;
        }

        bool encode(const nlohmann::json& value) {
            switch(value.type()) {
                case nlohmann::detail::value_t::boolean:
                    return pb_encode_tag_fixed(pson_wire_type::discrete_t, value.get<bool>() ? 1 : 0);

                case nlohmann::detail::value_t::null:
                    return pb_encode_tag_fixed(pson_wire_type::discrete_t, 2);

                case nlohmann::detail::value_t::number_integer: {
                    int64_t signed_value = value.get<int64_t>();
                    if(signed_value < 0) {
                        return pb_encode_tag(pson_wire_type::signed_t, -signed_value);
                    }
                    return pb_encode_tag(pson_wire_type::unsigned_t, signed_value);
                }

                case nlohmann::detail::value_t::number_unsigned:
                    return pb_encode_tag(pson_wire_type::unsigned_t, value.get<uint64_t>());

                case nlohmann::detail::value_t::number_float: {
                    double double_value = value.get<double>();

                    // Check if it can be saved as integer
                    int64_t int_value = static_cast<int64_t>(double_value);
                    if(int_value == double_value) {
                        if(int_value < 0) {
                            return pb_encode_tag(pson_wire_type::signed_t, -int_value);
                        }
                        return pb_encode_tag(pson_wire_type::unsigned_t, int_value);
                    }

                    // Check if float precision is enough
                    float float_value = static_cast<float>(double_value);
                    if(float_value == double_value) {
                        return pb_encode_float(float_value);
                    }

                    // Use double
                    return pb_encode_double(double_value);
                }

                case nlohmann::detail::value_t::string:
                    return pb_encode_string(value.get<std::string>().c_str());

                case nlohmann::detail::value_t::array:
                    return encode_array(value);

                case nlohmann::detail::value_t::object:
                    return encode_object(value);

                case nlohmann::detail::value_t::binary: {
                    auto& binary = value.get_binary();
                    return pb_encode_bytes(binary.data(), binary.size());
                }

                default:
                    return false;
            }
        }

    private:
        Writer& writer_;

        bool write(const void* data, size_t size = 1) {
            return writer_.write(data, size);
        }
    };

}

#endif