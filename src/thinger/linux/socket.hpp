// The MIT License (MIT)
//
// Copyright (c) 2020 INTERNET OF THINGER SL
// Author: alvarolb@thinger.io (Alvaro Luis Bustamante)
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

#ifndef THINGER_CLIENT_SOCKET_HPP
#define THINGER_CLIENT_SOCKET_HPP

#include <memory>
#include <functional>
#include <thread>
#include <iostream>

#include <boost/asio.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/regex.hpp>
#include <loguru.hpp>

namespace thinger_client {

class socket : private boost::asio::noncopyable
{

protected:
    boost::asio::ip::tcp::socket socket_;
    boost::asio::io_service& io_service_;
    bool time_out_ = false;

public:
    socket(boost::asio::io_service& io_service) : socket_(io_service), io_service_(io_service)
    {

    }

    virtual ~socket(){
        close();
    }

public:

    void connect(const std::string& host, const std::string& port, unsigned int expire_seconds, std::function<void(const boost::system::error_code&)> handler){

        // resolve socket host
        std::shared_ptr<boost::asio::ip::tcp::resolver> resolver = std::make_shared<boost::asio::ip::tcp::resolver>(get_io_service());
        boost::asio::ip::tcp::resolver::query query(host, port);

        LOG_F(1, "resolving: %s:%s", host.c_str(), port.c_str());

        resolver->async_resolve(query, [this, host, expire_seconds, handler, resolver](const boost::system::error_code& ec, boost::asio::ip::tcp::resolver::iterator it){
            if(ec){
                LOG_F(ERROR, "cannot resolve host: %s", ec.message().c_str());
                return handler(ec);
            }

            LOG_F(1, "dns query resolved");

            // reset connection timeout
            time_out_ = false;

            std::shared_ptr<boost::asio::deadline_timer> timer = std::make_shared<boost::asio::deadline_timer>(get_io_service());
            timer->expires_from_now(boost::posix_time::seconds(expire_seconds));
            timer->async_wait([this](const boost::system::error_code& ec){
                // if handler is called without a cancelled reason (should happen when connect is succeed)
                if(ec!=boost::asio::error::operation_aborted){
                    LOG_F(1, "connection timed out!");
                    time_out_ = true;
                    boost::system::error_code ecc;
                    socket_.cancel(ecc);
                }
            });

            boost::asio::ip::tcp::endpoint ep = it->endpoint();
            LOG_F(1, "target host is located at: %s (%d)", ep.address().to_v4().to_string().c_str(), ep.port());

            // connect socket
            boost::asio::async_connect(get_socket(), it, [this, host, timer, handler=std::move(handler)](const boost::system::error_code& ec, boost::asio::ip::tcp::resolver::iterator it){

                // check any errors to return
                if(ec){
                    auto ecc = time_out_ ? boost::asio::error::timed_out : ec;
                    LOG_F(1, "connection to host failed, reason: %s", ecc.message().c_str());
                    return handler(ecc);
                }

                LOG_F(1, "socket connected");

                // connected, then cancel timeout timer
                timer->cancel();
                LOG_F(1, "timeout cancelled");

                // enable tcp no delay
                enable_tcp_no_delay();

                // handle handshake
                if(requires_handshake()){
                    LOG_F(1, "running SSL handshake");
                    run_handshake([this, handler=std::move(handler)](const boost::system::error_code& ec){
                        if(ec){
                            LOG_F(ERROR, "error while running SSL Handshake: %s", ec.message().c_str());
                        }else{
                            LOG_F(1, "handshake succeed!");
                        }
                        handler(ec);
                    }, host);
                }else{
                    // just provide the provided ec (no error)
                    LOG_F(1, "no SSL handshake required");
                    handler(ec);
                }
            });
        });
    }

    boost::asio::io_service& get_io_service()
    {
        return io_service_;
    }

    boost::asio::ip::tcp::socket& get_socket(){
        return socket_;
    }

    std::string get_remote_ip()
    {
        boost::system::error_code ec;
        boost::asio::ip::tcp::endpoint remote_ep = socket_.remote_endpoint(ec);
        if(!ec){
            boost::asio::ip::address remote_ad = remote_ep.address();
            return remote_ad.to_string();
        }else{
            // todo review this constant, look at default ip address in boost socket ipv4/ipv6
            return "0.0.0.0";
        }
    }

    std::string get_local_port(){
        boost::system::error_code ec;
        boost::asio::ip::tcp::endpoint local_ep = socket_.local_endpoint(ec);
        if(!ec){
            return std::to_string(local_ep.port());
        }else{
            return "0";
        }
    }

    std::string get_remote_port(){
        boost::system::error_code ec;
        boost::asio::ip::tcp::endpoint remote_ep = socket_.remote_endpoint(ec);
        if(!ec){
            return std::to_string(remote_ep.port());
        }else{
            return "0";
        }
    }

    void cancel(){
        socket_.cancel();
    }

    /**
    * Indicate whether this socket requires a handshake, i.e., for an SSL connection. If it
    * returns true, then run_connection_handshake will be called;
    */
    virtual bool requires_handshake() const{
        return false;
    }

    /**
     * Called when the socket is connected an initialized. Must be implemented by
     * underlying socket, i.e., a SSL Socket must start the handshake in this moment.
     */
    virtual void run_handshake(std::function<void(const boost::system::error_code& error)> callback, const std::string& host="")
    {
        static const auto no_error = boost::system::error_code();
        callback(no_error);
    }

    /**
    * Can be called to close the actual socket connection in a graceful way.
    */
	virtual void close();

	virtual size_t read(uint8_t buffer[], size_t size, boost::system::error_code& ec){
        return boost::asio::read(socket_,  boost::asio::buffer(buffer, size), ec);
    }

    virtual size_t write(uint8_t buffer[], size_t size, boost::system::error_code& ec){
        return boost::asio::write(socket_,  boost::asio::const_buffer(buffer, size), ec);
    }

    virtual void async_read_some(uint8_t buffer[], size_t max_size, std::function<void(const boost::system::error_code& e, std::size_t bytes_transferred)> handler)
    {
        socket_.async_read_some(boost::asio::buffer(buffer, max_size), handler);
    }

    virtual void async_write_some(uint8_t buffer[], size_t size, std::function<void(const boost::system::error_code& e, std::size_t bytes_transferred)> handler)
    {
        socket_.async_write_some(boost::asio::buffer(buffer, size), handler);
    }

    virtual void async_write_some(std::function<void(const boost::system::error_code& e, std::size_t bytes_transferred)> handler)
    {
        socket_.async_write_some(boost::asio::null_buffers(), handler);
    }

    virtual void async_read(uint8_t buffer[], size_t size, std::function<void(const boost::system::error_code& e, std::size_t bytes_transferred)> handler)
    {
        boost::asio::async_read(socket_, boost::asio::buffer(buffer, size), boost::asio::transfer_exactly(size), handler);
    }

    virtual void async_read(boost::asio::streambuf& buffer, size_t size, std::function<void(const boost::system::error_code& e, std::size_t bytes_transferred)> handler)
    {
        boost::asio::async_read(socket_, buffer, boost::asio::transfer_exactly(size), handler);
    }

    virtual void async_read_until(boost::asio::streambuf& buffer, const boost::regex & expr, std::function<void(const boost::system::error_code& e, std::size_t bytes_transferred)> handler)
    {
        boost::asio::async_read_until(socket_, buffer, expr, handler);
    }

    virtual void async_read_until(boost::asio::streambuf& buffer, const std::string & delim, std::function<void(const boost::system::error_code& e, std::size_t bytes_transferred)> handler)
    {
        boost::asio::async_read_until(socket_, buffer, delim, handler);
    }

    virtual void async_write(uint8_t buffer[], size_t size, std::function<void(const boost::system::error_code& e, std::size_t bytes_transferred)> handler)
    {
        boost::asio::async_write(socket_, boost::asio::buffer(buffer, size), boost::asio::transfer_exactly(size), handler);
    }

    virtual void async_write(const std::string& str, std::function<void(const boost::system::error_code& e, std::size_t bytes_transferred)> handler)
    {
        boost::asio::async_write(socket_, boost::asio::buffer(str), handler);
    }

    virtual void async_write(const std::vector<boost::asio::const_buffer>& buffer, std::function<void(const boost::system::error_code& e, std::size_t bytes_transferred)> handler)
    {
        boost::asio::async_write(socket_, buffer, handler);
    }

    bool enable_tcp_no_delay(){
        boost::system::error_code ec;
        boost::asio::ip::tcp::no_delay option(true);
        socket_.set_option(option, ec);
        if(ec) LOG_F(ERROR, "error enabling TCP no delay: %s", ec.message().c_str());
        return !ec;
    }

    bool disable_tcp_no_delay(){
        boost::system::error_code ec;
        boost::asio::ip::tcp::no_delay option(false);
        socket_.set_option(option, ec);
        if(ec) LOG_F(ERROR, "error disabling TCP no delay: %s", ec.message().c_str());
        return !ec;
    }

    size_t available(){
	    boost::system::error_code ec;
	    auto size = socket_.available(ec);
	    if(ec) LOG_F(ERROR, "error while getting socket available bytes (%zu): %s", size, ec.message().c_str());
	    return size;
    }

    bool is_open() const{
        return socket_.is_open();
    }

    virtual bool is_secure(){
        return false;
    }
};

}

#endif
