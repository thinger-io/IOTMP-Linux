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

#ifndef THINGER_LINUX_ASIO_CLIENT_HPP
#define THINGER_LINUX_ASIO_CLIENT_HPP

#include <memory>
#include <iostream>

#include "../core/thinger_log.hpp"
#include "../core/thinger.h"
#ifdef THINGER_OPEN_SSL
#include "ssl_socket.hpp"
#else
#include "socket.hpp"
#endif

#include "async_timer.hpp"

namespace thinger_client{

    class asio_client : public ::thinger_client::thinger, public std::enable_shared_from_this<asio_client>{

        static const int WRITE_SOCKET_BUFFER = 8192;

#ifdef THINGER_OPEN_SSL
        static std::shared_ptr <boost::asio::ssl::context> get_ssl_ctx(){
            auto ssl_ctx = std::make_shared<boost::asio::ssl::context>(boost::asio::ssl::context::sslv23_client);
            ssl_ctx->set_verify_mode(boost::asio::ssl::verify_none);
            return ssl_ctx;
        }
#endif

    public:
        asio_client(const std::string& user, const std::string& device, const std::string& device_credential) :
                work_(io_),
                username_(user),
                device_id_(device),
                device_password_(device_credential),
#ifdef THINGER_OPEN_SSL
                socket_(io_, get_ssl_ctx()),
#else
                socket_(io_),
#endif
                async_timer_(io_)
        {


        }

        asio_client() :
                work_(io_),
#ifdef THINGER_OPEN_SSL
                socket_(io_, get_ssl_ctx()),
#else
                socket_(io_),
#endif
                async_timer_(io_)
        {

        }

        virtual ~asio_client()
        {

        }

        void set_credentials(const std::string& user, const std::string& device, const std::string& device_credential){
            username_ = user;
            device_id_ = device;
            device_password_ = device_credential;
        }

        void start() {
            if(running_) return;
            running_ = true;
            THINGER_LOG_TAG("CLIENT", "Starting ASIO client...");
            connect();
            // restart, so it can start again after an stop
            io_.restart();
            io_.run();
            THINGER_LOG_TAG("CLIENT", "Stopped ASIO client...");
        }

        void stop() {
            if(!running_) return;
            running_ = false;
            THINGER_LOG_TAG("CLIENT", "Stopping ASIO client...");
            socket_.close();
            async_timer_.stop();
            io_.stop();
        }

        boost::asio::io_service& get_io_service(){
            return io_;
        }

        bool connected() const{
            return socket_.is_open() && authenticated_;
        }

    protected:

        bool read(char* buffer, size_t size) override{
            boost::system::error_code ec;
            auto read = socket_.read((uint8_t *)buffer, size, ec);
            bool success = !ec && read==size;
            if(ec != boost::asio::error::operation_aborted && !success){
                notify_state(SOCKET_READ_ERROR, ec.message().c_str());
            }
            return success;
        }

        bool do_write(uint8_t* buffer, size_t size){
            if(size>0){
                boost::system::error_code ec;
                //LOG_F(INFO, "writing %zu (bytes)", size);
                auto write = socket_.write(buffer, size, ec);
                bool success = !ec && write==size;
                if(ec != boost::asio::error::operation_aborted && !success){
                    notify_state(SOCKET_WRITE_ERROR, ec.message().c_str());
                }
                return success;
            }
            return true;
        }

        bool flush_out_buffer(){
            if(out_size_>0){
                bool success = do_write(out_buffer_, out_size_);
                out_size_ = 0;
                return success;
            }
            return true;
        }

        bool write(const char* buffer, size_t size, bool flush) override{
            //LOG_F(INFO, "out_size: %zu, received: %zu, flush: %d, force_flush: %d", out_size_, size, flush, out_size_+size>SOCKET_BUFFER);
            if(size>0){

                // incoming data does not fit in buffer? flush current content to free buffer
                if(out_size_>0 && out_size_+size > WRITE_SOCKET_BUFFER) {
                    //LOG_F(INFO, "out size is: %zu, incoming size is: %zu, flushing buffer...", out_size_, size);
                    if(!flush_out_buffer()) return false;
                }

                // current frame fits in buffer?
                if(size > WRITE_SOCKET_BUFFER){
                    //LOG_F(INFO, "out size is: %zu, incoming size is: %zu", out_size_, size);
                    return do_write((uint8_t*)buffer, size);
                }

                // copy incoming data into output buffer
                //LOG_F(INFO, "copying content to buffer: %zu", size);
                memcpy(out_buffer_+out_size_, buffer, size);
                out_size_ += size;
                //LOG_F(INFO, "current out size: %zu", out_size_);
            }

            if(flush){
                return flush_out_buffer();
            }

            return true;
        }

        void disconnected() override{
            out_size_ = 0;
            authenticated_ = false;
            notify_state(SOCKET_DISCONNECTED);
            thinger::disconnected();
            socket_.close();
            async_timer_.stop();
            if(running_){
                connect();
            }
        }

        void wait_for_input(){
            socket_.get_socket().async_wait(boost::asio::ip::tcp::socket::wait_read, [this](const boost::system::error_code& ec){
                if(ec){
                    LOG_F(ERROR, "async_wait error: %s", ec.message().c_str());
                    notify_state(SOCKET_READ_ERROR, ec.message().c_str());
                    if(ec != boost::asio::error::operation_aborted){
                        return disconnected();
                    }
                    return; // just stop reading
                }

                auto available = socket_.available();
                if(available){
                    //LOG_F(INFO, "received data: %zu", available);
                    do{
                        auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                        thinger::handle(timestamp, true);
                    }while(socket_.available());
                    wait_for_input();
                }else{
                    notify_state(SOCKET_READ_ERROR, "no available bytes...");
                    disconnected();
                }
            });
        }

        void connect(){
            if(connecting_ || socket_.is_open()){
                //LOG_F(WARNING, "preventing a double connecting. connecting: %d, socket_open: %d", connecting_, socket_.is_open());
                return;
            }

            // set connecting to true
            connecting_ = true;

            notify_state(SOCKET_CONNECTING);


            socket_.connect(THINGER_SERVER, std::to_string(THINGER_PORT), THINGER_CONNECT_TIMEOUT, [this](const boost::system::error_code& e) -> void{
                // reset connecting
                connecting_ = false;


                if(e) {
                    if(e != boost::asio::error::operation_aborted){
                        notify_state(SOCKET_CONNECTION_ERROR, e.message().c_str());
                        std::this_thread::sleep_for(std::chrono::seconds(THINGER_RECONNECT_SECONDS));
                        return disconnected();
                    }
                    return;
                }

                notify_state(SOCKET_CONNECTED);
                notify_state(THINGER_AUTHENTICATING);

                if(thinger::connect(username_.c_str(), device_id_.c_str(), device_password_.c_str())){
                    notify_state(THINGER_AUTHENTICATED);

                    // set authenticated flag
                    authenticated_ = true;

                    // read input
                    wait_for_input();

                    // start timer for streams & timeouts
                    async_timer_.set_timeout(1);
                    async_timer_.set_callback([this]{
                        auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                        thinger::handle(timestamp, false);
                    });
                    async_timer_.start();

                    // subscribe to resources
                    thinger::subscribe_resources();
                }else{
                    notify_state(THINGER_AUTH_FAILED);
                    std::this_thread::sleep_for(std::chrono::seconds(THINGER_RECONNECT_SECONDS));
                    return disconnected();
                }
            });

        }

    protected:
        boost::asio::io_service io_;
        boost::asio::io_service::work work_;
        async_timer async_timer_;
        uint8_t out_buffer_[WRITE_SOCKET_BUFFER];
        size_t out_size_ = 0;
        bool authenticated_ = false;

#ifdef THINGER_OPEN_SSL
        ssl_socket socket_;
#else
        socket socket_;
#endif
        bool running_ = false;
        bool connecting_ = false;

        // device credentials
        std::string username_;
        std::string device_id_;
        std::string device_password_;
    };

}


#endif
