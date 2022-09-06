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

#ifndef THINGER_CLIENT_SSL_SOCKET_HPP
#define THINGER_CLIENT_SSL_SOCKET_HPP

#include "socket.hpp"
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

namespace thinger_client {

class ssl_socket : public socket {
public:
	ssl_socket(boost::asio::io_service& io_service, std::shared_ptr<boost::asio::ssl::context> ssl_context) :
			socket(io_service),
			ssl_context_(ssl_context),
			ssl_stream_(socket_, *ssl_context)
	{

	}

	virtual ~ssl_socket();

private:
	boost::asio::ssl::stream<boost::asio::ip::tcp::socket&> ssl_stream_;
    std::shared_ptr<boost::asio::ssl::context> ssl_context_;

public:

    void close() override{
        // shutdown ssl stream
        //boost::system::error_code ec;
        //ssl_stream_.shutdown(ec);

        // clear ssl session to allow reusing socket (if necessary)
        // From SSL_clear If a session is still open, it is considered bad and will be removed from the session cache, as required by RFC2246
        SSL_clear(ssl_stream_.native_handle());

        // close underlying tcp socket
        socket::close();
    }

	virtual bool requires_handshake() const{
		return true;
	}

	virtual void run_handshake(std::function<void(const boost::system::error_code& error)> callback, const std::string& host=""){
        //ssl_stream_.set_verify_mode(boost::asio::ssl::verify_peer);
        //ssl_stream_.set_verify_callback(boost::asio::ssl::rfc2818_verification(host));
        //ssl_stream_.set_verify_mode(boost::asio::ssl::verify_none);
        ssl_stream_.async_handshake(boost::asio::ssl::stream_base::client, [callback=std::move(callback)](const boost::system::error_code& ec){
            callback(ec);
        });
	}

    virtual size_t read(uint8_t buffer[], size_t size, boost::system::error_code& ec){
        return boost::asio::read(ssl_stream_,  boost::asio::buffer(buffer, size), ec);
    }

    virtual size_t write(uint8_t buffer[], size_t size, boost::system::error_code& ec){
        return boost::asio::write(ssl_stream_, boost::asio::const_buffer(buffer, size), ec);
    }

	virtual void async_read_some(uint8_t buffer[], size_t max_size, std::function<void(const boost::system::error_code& e, std::size_t bytes_transferred)> handler)
	{
		ssl_stream_.async_read_some(boost::asio::buffer(buffer, max_size), handler);
	}

	virtual void async_write_some(std::function<void(const boost::system::error_code& e, std::size_t bytes_transferred)> handler)
	{
		ssl_stream_.async_write_some(boost::asio::null_buffers(), handler);
	}

	virtual void async_write_some(uint8_t buffer[], size_t size, std::function<void(const boost::system::error_code& e, std::size_t bytes_transferred)> handler)
	{
		ssl_stream_.async_write_some(boost::asio::buffer(buffer, size), handler);
	}

	virtual void async_read(uint8_t buffer[], size_t size, std::function<void(const boost::system::error_code& e, std::size_t bytes_transferred)> handler)
	{
		boost::asio::async_read(ssl_stream_, boost::asio::buffer(buffer, size), boost::asio::transfer_exactly(size), handler);
	}

	virtual void async_read_until(boost::asio::streambuf& buffer, const boost::regex & expr, std::function<void(const boost::system::error_code& e, std::size_t bytes_transferred)> handler)
	{
		boost::asio::async_read_until(ssl_stream_, buffer, expr, handler);
	}

	virtual void async_write(uint8_t buffer[], size_t size, std::function<void(const boost::system::error_code& e, std::size_t bytes_transferred)> handler)
	{
		boost::asio::async_write(ssl_stream_, boost::asio::buffer(buffer, size), boost::asio::transfer_exactly(size), handler);
	}

	virtual void async_write(const std::string& str, std::function<void(const boost::system::error_code& e, std::size_t bytes_transferred)> handler)
	{
		boost::asio::async_write(ssl_stream_, boost::asio::buffer(str), handler);
	}

	virtual void async_write(const std::vector<boost::asio::const_buffer>& buffer, std::function<void(const boost::system::error_code& e, std::size_t bytes_transferred)> handler)
	{
		boost::asio::async_write(ssl_stream_, buffer, handler);
	}

	virtual bool is_secure(){
		return true;
	}
};

}

#endif
