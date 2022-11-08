#ifndef THINGER_ASIO_SSL_SOCKET_HPP
#define THINGER_ASIO_SSL_SOCKET_HPP

#include "tcp_socket.hpp"
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

namespace thinger::asio{

class ssl_socket : public tcp_socket {
public:
    // constructors and destructors
    ssl_socket(const std::string& context, boost::asio::io_service& io_service, std::shared_ptr<boost::asio::ssl::context> ssl_context);
	ssl_socket(const std::string& context, std::shared_ptr<tcp_socket> socket, std::shared_ptr<boost::asio::ssl::context> ssl_context);
	virtual ~ssl_socket();

    // socket control
    void close() override;
	bool requires_handshake() const override;
	void run_handshake(ec_handler callback, const std::string& host="") override;

    // read
    size_t read(uint8_t buffer[], size_t size, boost::system::error_code& ec) override;
	void async_read_some(uint8_t buffer[], size_t max_size, ec_size_handler handler) override;
	void async_write_some(ec_size_handler handler) override;
	void async_write_some(uint8_t buffer[], size_t size, ec_size_handler handler) override;
	void async_read(uint8_t buffer[], size_t size, ec_size_handler handler) override;
    void async_read(boost::asio::streambuf&, size_t, std::function<void(const boost::system::error_code&, std::size_t)>) override;
	void async_read_until(boost::asio::streambuf& buffer, const boost::regex & expr, ec_size_handler handler) override;
    void async_read_until(boost::asio::streambuf &buffer, const std::string &delim, ec_size_handler handler) override;

    // write
    size_t write(uint8_t buffer[], size_t size, boost::system::error_code& ec) override;
    void async_write(uint8_t buffer[], size_t size, ec_size_handler handler) override;
	void async_write(const std::string& str, ec_size_handler handler) override;
	void async_write(const std::vector<boost::asio::const_buffer>& buffer, ec_size_handler handler) override;

    // some getters to check the state
    bool is_secure() const override;

private:
	boost::asio::ssl::stream<boost::asio::ip::tcp::socket&> ssl_stream_;
	std::shared_ptr<boost::asio::ssl::context> ssl_context_;
};

}

#endif
