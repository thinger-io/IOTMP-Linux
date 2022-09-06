#ifndef THINGER_SSL_SOCKET_HPP
#define THINGER_SSL_SOCKET_HPP

#include "tcp_socket.hpp"
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

namespace thinger{

class ssl_socket : public tcp_socket {
public:
	ssl_socket(const std::string& context, boost::asio::io_service& io_service, std::shared_ptr<boost::asio::ssl::context> ssl_context);
	ssl_socket(const std::string& context, std::shared_ptr<tcp_socket> socket, std::shared_ptr<boost::asio::ssl::context> ssl_context);

	virtual ~ssl_socket();

	void close() override;

	bool requires_handshake() const override;

	void run_handshake(std::function<void(const boost::system::error_code& error)> callback, const std::string& host="") override;

	void async_read_some(uint8_t buffer[], size_t max_size, std::function<void(const boost::system::error_code& e, std::size_t bytes_transferred)> handler) override;

	void async_write_some(std::function<void(const boost::system::error_code& e, std::size_t bytes_transferred)> handler) override;

	void async_write_some(uint8_t buffer[], size_t size, std::function<void(const boost::system::error_code& e, std::size_t bytes_transferred)> handler) override;

	void async_read(uint8_t buffer[], size_t size, std::function<void(const boost::system::error_code& e, std::size_t bytes_transferred)> handler) override;

    void async_read(boost::asio::streambuf&, size_t, std::function<void(const boost::system::error_code&, long unsigned int)>) override;

	void async_read_until(boost::asio::streambuf& buffer, const boost::regex & expr, std::function<void(const boost::system::error_code& e, std::size_t bytes_transferred)> handler) override;

    void async_read_until(boost::asio::streambuf &buffer, const std::string &delim, std::function<void(const boost::system::error_code &e, std::size_t bytes_transferred)> handler) override;

	void async_write(uint8_t buffer[], size_t size, std::function<void(const boost::system::error_code& e, std::size_t bytes_transferred)> handler) override;

	void async_write(const std::string& str, std::function<void(const boost::system::error_code& e, std::size_t bytes_transferred)> handler) override;

	void async_write(const std::vector<boost::asio::const_buffer>& buffer, std::function<void(const boost::system::error_code& e, std::size_t bytes_transferred)> handler) override;

	bool is_secure() const override;

private:
	boost::asio::ssl::stream<boost::asio::ip::tcp::socket&> ssl_stream_;
	std::shared_ptr<boost::asio::ssl::context> ssl_context_;
};

}

#endif
