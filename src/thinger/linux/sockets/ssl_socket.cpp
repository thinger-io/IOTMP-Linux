#include "ssl_socket.hpp"
#include <loguru.hpp>

namespace thinger{
/*
	namespace {
		int servername_callback(SSL* ssl, int*, void* arg)
		{
			// get SNI server name
			const char* hostname = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);

			if(hostname!=NULL){
				LOG_F(3, "SNI connection for: %s", hostname);
				auto certificate = server::certificates.get_certificate(hostname);
				if(certificate){
					auto ssl_ctx = certificate->native_handle();
					SSL_set_SSL_CTX(ssl, ssl_ctx);
					SSL_set_verify(ssl, SSL_CTX_get_verify_mode(ssl_ctx), SSL_CTX_get_verify_callback(ssl_ctx));
				}else{
					LOG_F(WARNING, "using default server certificate for hostname: %s", hostname);
				}
			}

			return SSL_TLSEXT_ERR_OK;
		}
	}
 */

    ssl_socket::ssl_socket(const std::string& context, boost::asio::io_service& io_service, std::shared_ptr<boost::asio::ssl::context> ssl_context) :
            tcp_socket(context, io_service),
            ssl_stream_(socket_, *ssl_context),
            ssl_context_(ssl_context)
    {

    }

    ssl_socket::ssl_socket(const std::string &context, std::shared_ptr<tcp_socket> socket, std::shared_ptr<boost::asio::ssl::context> ssl_context) :
        tcp_socket(context, socket),
        ssl_stream_(socket_, *ssl_context),
        ssl_context_(ssl_context)
    {

    }

	void ssl_socket::close() {
		//if(is_open()){
			// graceful SSL disconnect (do not call shutdown over boost asio, as it will wait for remote endpoint confirmation)
		//	SSL_shutdown(ssl_stream_.native_handle());
		//}

		// close socket
		tcp_socket::close();

		// clear ssl session to allow reusing socket (if necessary)
		// From SSL_clear If a session is still open, it is considered bad and will be removed from the session cache, as required by RFC2246
		SSL_clear(ssl_stream_.native_handle());
	}

    ssl_socket::~ssl_socket() {
        LOG_F(3, "releasing ssl connection");
    }

    bool ssl_socket::requires_handshake() const{
        return true;
    }

    void ssl_socket::run_handshake(std::function<void(const boost::system::error_code& error)> callback, const std::string& host){
        if(!host.empty()){
            // add support for SNI
            if(!SSL_set_tlsext_host_name(ssl_stream_.native_handle(), host.c_str())){
            	LOG_F(ERROR, "SSL_set_tlsext_host_name failed. SNI will fail");
            }

            // set verify parameters for SSL
            ssl_stream_.set_verify_mode(boost::asio::ssl::verify_peer);
            ssl_stream_.set_verify_callback(boost::asio::ssl::rfc2818_verification(host));
            ssl_stream_.async_handshake(boost::asio::ssl::stream_base::client, [callback](const boost::system::error_code& ec){
                callback(ec);
            });
        }else{
	        // add support for SNI
	        //SSL_CTX_set_tlsext_servername_callback(ssl_context_->native_handle(), servername_callback);

	        // handle handshake
            ssl_stream_.async_handshake(boost::asio::ssl::stream_base::server, [callback](const boost::system::error_code& ec){
                callback(ec);
            });
        }
    }

    void ssl_socket::async_read_some(uint8_t buffer[], size_t max_size, std::function<void(const boost::system::error_code& e, std::size_t bytes_transferred)> handler)
    {
        ssl_stream_.async_read_some(boost::asio::buffer(buffer, max_size), handler);
    }

    void ssl_socket::async_write_some(std::function<void(const boost::system::error_code& e, std::size_t bytes_transferred)> handler)
    {
        ssl_stream_.async_write_some(boost::asio::null_buffers(), handler);
    }

    void ssl_socket::async_write_some(uint8_t buffer[], size_t size, std::function<void(const boost::system::error_code& e, std::size_t bytes_transferred)> handler)
    {
        ssl_stream_.async_write_some(boost::asio::buffer(buffer, size), handler);
    }

    void ssl_socket::async_read(uint8_t buffer[], size_t size, std::function<void(const boost::system::error_code& e, std::size_t bytes_transferred)> handler)
    {
        boost::asio::async_read(ssl_stream_, boost::asio::buffer(buffer, size), boost::asio::transfer_exactly(size), handler);
    }

    void ssl_socket::async_read(boost::asio::streambuf &buffer, size_t size,
                            std::function<void(const boost::system::error_code &e, std::size_t bytes_transferred)> handler) {
        boost::asio::async_read(ssl_stream_, buffer, boost::asio::transfer_exactly(size), handler);
    }

    void ssl_socket::async_read_until(boost::asio::streambuf& buffer, const boost::regex & expr, std::function<void(const boost::system::error_code& e, std::size_t bytes_transferred)> handler)
    {
        boost::asio::async_read_until(ssl_stream_, buffer, expr, handler);
    }

    void ssl_socket::async_read_until(boost::asio::streambuf &buffer, const std::string &delim, std::function<void(const boost::system::error_code &e, std::size_t bytes_transferred)> handler)
    {
        boost::asio::async_read_until(ssl_stream_, buffer, delim, handler);
    }

    void ssl_socket::async_write(uint8_t buffer[], size_t size, std::function<void(const boost::system::error_code& e, std::size_t bytes_transferred)> handler)
    {
        boost::asio::async_write(ssl_stream_, boost::asio::buffer(buffer, size), boost::asio::transfer_exactly(size), handler);
    }

    void ssl_socket::async_write(const std::string& str, std::function<void(const boost::system::error_code& e, std::size_t bytes_transferred)> handler)
    {
        boost::asio::async_write(ssl_stream_, boost::asio::buffer(str), handler);
    }

    void ssl_socket::async_write(const std::vector<boost::asio::const_buffer>& buffer, std::function<void(const boost::system::error_code& e, std::size_t bytes_transferred)> handler)
    {
        boost::asio::async_write(ssl_stream_, buffer, handler);
    }

    bool ssl_socket::is_secure() const{
        return true;
    }


}
