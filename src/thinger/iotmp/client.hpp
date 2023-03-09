// The MIT License (MIT)
//
// Copyright (c) 2020 INTERNET OF THINGER SL
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

#ifndef THINGER_IOTMP_CLIENT_HPP
#define THINGER_IOTMP_CLIENT_HPP

#include <memory>
#include <iostream>

#include "core/iotmp.h"
#include "../asio/workers.hpp"
#include "../asio/sockets/ssl_socket.hpp"
#include "../asio/sockets/websocket.hpp"
#include "../util/async_timer.hpp"
#include "../util/logger.hpp"

namespace thinger::iotmp{

    class client : public iotmp{

        static const int WRITE_SOCKET_BUFFER = 8192;

    public:
        
        client(std::string transport = "", bool shared_pool = false);
        virtual ~client() = default;

        void set_credentials(const std::string& user, const std::string& device, const std::string& device_credential);
        void set_hostname(const std::string& hostname);
        const std::string& get_user() const;
        const std::string& get_device() const;
        const std::string& get_credentials() const;
        const std::string& get_hostname() const;

        bool connected() const;
        void start(std::function<void(exec_result)> callback = {});
        void stop(std::function<void(exec_result)> callback = {});
        bool run(std::function<bool()> callback);

        boost::asio::io_service& get_io_service();

    protected:

        void wait_for_input();
        bool read(char* buffer, size_t size) override;

        bool write(const char* buffer, size_t size, bool flush) override;
        bool do_write(uint8_t* buffer, size_t size);
        bool flush_out_buffer();

        void connect();
        void authenticate();
        void disconnected() override;

        std::unique_ptr<thinger::asio::socket> socket_;
        std::string transport_;

        boost::asio::io_service& io_service_;
        thinger::util::async_timer async_timer_;

        uint8_t out_buffer_[WRITE_SOCKET_BUFFER];
        size_t out_size_ = 0;

        bool authenticated_ = false;
        bool running_ = false;
        bool connecting_ = false;

        // device credentials
        std::string username_;
        std::string device_id_;
        std::string device_password_;
        std::string hostname_;
    };

}


#endif
