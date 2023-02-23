#include <iostream>
#include "tcp_socket.hpp"

namespace thinger::asio {

    void tcp_socket::close() {
	    boost::system::error_code ec;
	    if(socket_.is_open()){
		    socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
	    }
        socket_.close(ec);
        LOG_LEVEL(3, "closing tcp socket result: %s", ec.message().c_str());
    }

    tcp_socket::tcp_socket(const std::string& context, boost::asio::io_service& io_service) :
            socket(context, io_service), socket_(io_service) {

    }

    tcp_socket::tcp_socket(const std::string& context, const std::shared_ptr<tcp_socket>& tcp_socket) :
            socket(context, tcp_socket->get_io_service()), socket_(std::move(tcp_socket->get_socket())) {

    }

    tcp_socket::~tcp_socket(){
        LOG_LEVEL(3, "releasing tcp connection");
        close();
    }

    void tcp_socket::connect(const std::string& host, const std::string& port, std::chrono::seconds expire_seconds,
                             ec_handler handler){

        // resolve socket host
        std::shared_ptr<boost::asio::ip::tcp::resolver> resolver = std::make_shared<boost::asio::ip::tcp::resolver>(get_io_service());
        boost::asio::ip::tcp::resolver::query query(host, port);

        resolver->async_resolve(query,
                                [this, host, expire_seconds, handler=std::move(handler), resolver](const boost::system::error_code &ec, boost::asio::ip::tcp::resolver::iterator it) {

                                    // any error during resolve
                                    if (ec) {
                                        return handler(ec);
                                    }

                                    // reset connection timeout
                                    time_out_ = false;

                                    // initialize a connection timeout
                                    std::shared_ptr<boost::asio::deadline_timer> timer = std::make_shared<boost::asio::deadline_timer>(get_io_service());
                                    timer->expires_from_now(boost::posix_time::seconds(expire_seconds.count()));
                                    timer->async_wait([this](boost::system::error_code ec) {
                                        // if handler is called without a cancelled reason (should happen when connect is succeed)
                                        if(ec!=boost::asio::error::operation_aborted){
                                            LOG_LEVEL(1, "connection timed out!");
                                            time_out_ = true;
                                            boost::system::error_code ecc;
                                            socket_.cancel(ecc);
                                        }
                                    });

                                    // connect socket
                                    boost::asio::async_connect(get_socket(), it,
                                                               [this, host, timer, handler=std::move(handler)](const boost::system::error_code &ec, boost::asio::ip::tcp::resolver::iterator it) {

                                                                   // check any errors to return
                                                                   if (ec) {
                                                                       auto ecc = time_out_ ? boost::asio::error::timed_out : ec;
                                                                       LOG_LEVEL(1, "connection to host failed, reason: %s", ecc.message().c_str());
                                                                       return handler(ecc);
                                                                   }

                                                                   // cancel timer
                                                                   timer->cancel();

                                                                   // enable tcp no delay
                                                                   enable_tcp_no_delay();

                                                                   // handle handshake
                                                                   if (requires_handshake()) {
                                                                       run_handshake([this, handler=std::move(handler)](const boost::system::error_code &ec) {
                                                                           if (ec) {
                                                                               LOG_ERROR("error while running SSL/TLS handshake: %s", ec.message().c_str());
                                                                           }
                                                                           handler(ec);
                                                                       }, host);
                                                                   } else {
                                                                       // just provide the provided ec (no error)
                                                                       handler(ec);
                                                                   }
                                                               });
                                });
    }

    boost::asio::ip::tcp::socket& tcp_socket::get_socket(){
        return socket_;
    }

    std::string tcp_socket::get_remote_ip() const{
        boost::system::error_code ec;
        boost::asio::ip::tcp::endpoint remote_ep = socket_.remote_endpoint(ec);
        if (!ec) {
            boost::asio::ip::address remote_ad = remote_ep.address();
            return remote_ad.to_string();
        } else {
            // todo review this constant, look at default ip address in boost socket ipv4/ipv6
            return "0.0.0.0";
        }
    }

    std::string tcp_socket::get_local_port() const{
        boost::system::error_code ec;
        boost::asio::ip::tcp::endpoint local_ep = socket_.local_endpoint(ec);
        if (!ec) {
            return std::to_string(local_ep.port());
        } else {
            return "0";
        }
    }

    std::string tcp_socket::get_remote_port() const{
        boost::system::error_code ec;
        boost::asio::ip::tcp::endpoint remote_ep = socket_.remote_endpoint(ec);
        if (!ec) {
            return std::to_string(remote_ep.port());
        } else {
            return "0";
        }
    }

    void tcp_socket::cancel(){
        socket_.cancel();
    }

    size_t tcp_socket::read(uint8_t* buffer, size_t size, boost::system::error_code& ec){
        return boost::asio::read(socket_,  boost::asio::buffer(buffer, size), ec);
    }

    size_t tcp_socket::write(uint8_t* buffer, size_t size, boost::system::error_code& ec){
        return boost::asio::write(socket_, boost::asio::const_buffer(buffer, size), ec);
    }

    void tcp_socket::async_read_some(uint8_t* buffer, size_t max_size, ec_size_handler handler){
        socket_.async_read_some(boost::asio::buffer(buffer, max_size), handler) ;
    }

    void tcp_socket::async_write_some(uint8_t* buffer, size_t size, ec_size_handler handler){
        socket_.async_write_some(boost::asio::buffer(buffer, size), handler);
    }

    void tcp_socket::async_write_some(ec_size_handler handler){
        socket_.async_write_some(boost::asio::null_buffers(), handler);
    }

    void tcp_socket::async_read(uint8_t* buffer, size_t size, ec_size_handler handler){
        boost::asio::async_read(socket_, boost::asio::buffer(buffer, size), boost::asio::transfer_exactly(size), handler);
    }

    void tcp_socket::async_read(boost::asio::streambuf& buffer, size_t size, ec_size_handler handler){
        boost::asio::async_read(socket_, buffer, boost::asio::transfer_exactly(size), handler);
    }

    void tcp_socket::async_read_until(boost::asio::streambuf& buffer, const boost::regex& expr, ec_size_handler handler){
        boost::asio::async_read_until(socket_, buffer, expr, handler);
    }

    void tcp_socket::async_read_until(boost::asio::streambuf& buffer, const std::string& delim, ec_size_handler handler){
        boost::asio::async_read_until(socket_, buffer, delim, handler);
    }

    void tcp_socket::async_write(uint8_t* buffer, size_t size, ec_size_handler handler){
        boost::asio::async_write(socket_, boost::asio::buffer(buffer, size), boost::asio::transfer_exactly(size), handler);
    }

    void tcp_socket::async_write(const std::string& str, ec_size_handler handler){
        boost::asio::async_write(socket_, boost::asio::buffer(str), handler);
    }

    void tcp_socket::async_write(const std::vector<boost::asio::const_buffer>& buffer, ec_size_handler handler){
        boost::asio::async_write(socket_, buffer, handler);
    }

    void tcp_socket::enable_tcp_no_delay(){
        boost::asio::ip::tcp::no_delay option(true);
        socket_.set_option(option);
    }

    void tcp_socket::disable_tcp_no_delay(){
        boost::asio::ip::tcp::no_delay option(false);
        socket_.set_option(option);
    }

    bool tcp_socket::is_open() const{
        return socket_.is_open();
    }

    bool tcp_socket::is_secure() const{
        return false;
    }

    size_t tcp_socket::available() const{
        boost::system::error_code ec;
        auto size = socket_.available(ec);
        if(ec) LOG_ERROR("error while getting socket available bytes (%zu): %s", size, ec.message().c_str());
        return size;
    }

    void tcp_socket::async_wait(boost::asio::socket_base::wait_type type,
                                std::function<void(const boost::system::error_code&)> handler){
        socket_.async_wait(type, std::move(handler));
    }

}