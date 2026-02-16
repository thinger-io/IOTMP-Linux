#ifndef THINGER_IOTMP_IO_ADAPTERS_HPP
#define THINGER_IOTMP_IO_ADAPTERS_HPP

#include <cstring>
#include <string>
#include <vector>

namespace thinger::iotmp {

    // Memory reader for decoding from memory buffers
    class memory_reader {
    private:
        const uint8_t* buffer_;
        size_t size_;
        size_t read_ = 0;

    public:
        memory_reader(const void* buffer, size_t size)
            : buffer_(static_cast<const uint8_t*>(buffer)), size_(size) {}

        bool read(void* target, size_t size) {
            if(read_ + size <= size_) {
                memcpy(target, buffer_ + read_, size);
                read_ += size;
                return true;
            }
            return false;
        }

        bool read(void* target) {
            return read(target, 1);
        }

        [[nodiscard]] size_t bytes_read() const {
            return read_;
        }
    };

    // String writer for encoding to std::string
    class string_writer {
    private:
        std::string& string_;
        size_t written_ = 0;

    public:
        explicit string_writer(std::string& string) : string_(string) {}

        bool write(const void* buffer, size_t size) {
            string_.append(static_cast<const char*>(buffer), size);
            written_ += size;
            return true;
        }

        [[nodiscard]] size_t bytes_written() const { return written_; }
    };

    // Null writer for size calculation (discards all data)
    class null_writer {
    private:
        size_t written_ = 0;

    public:
        bool write(const void*, size_t size) {
            written_ += size;
            return true;
        }

        [[nodiscard]] size_t bytes_written() const { return written_; }
    };

    // Memory writer for encoding to fixed-size buffers
    class memory_writer {
    private:
        uint8_t* buffer_;
        uint8_t* current_;
        uint8_t* end_;

    public:
        memory_writer(void* buffer, size_t size)
            : buffer_(static_cast<uint8_t*>(buffer)),
              current_(buffer_),
              end_(buffer_ + size) {}

        void reset() {
            current_ = buffer_;
        }

        [[nodiscard]] size_t bytes_written() const {
            return current_ - buffer_;
        }

        bool write(const void* data, size_t size) {
            const auto* src = static_cast<const uint8_t*>(data);
            while(current_ < end_ && size > 0) {
                *current_++ = *src++;
                --size;
            }
            return size == 0;
        }
    };

    // Vector writer - appends to a std::vector<uint8_t>, growing dynamically
    class vector_writer {
    private:
        std::vector<uint8_t>& buffer_;
        size_t written_ = 0;

    public:
        explicit vector_writer(std::vector<uint8_t>& buffer) : buffer_(buffer) {}

        bool write(const void* data, size_t size) {
            const auto* src = static_cast<const uint8_t*>(data);
            buffer_.insert(buffer_.end(), src, src + size);
            written_ += size;
            return true;
        }

        [[nodiscard]] size_t bytes_written() const { return written_; }
    };

}

#endif