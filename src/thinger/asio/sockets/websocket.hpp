#ifndef THINGER_ASIO_WEBSOCKET_HPP
#define THINGER_ASIO_WEBSOCKET_HPP

#include "socket.hpp"
#include <queue>

namespace thinger::asio{

struct pending_writes{
    uint8_t* buffer_;
    std::size_t   size_;
    ec_size_handler handler_;

    pending_writes(uint8_t* buffer, std::size_t size, ec_size_handler handler) :
        buffer_(buffer), size_(size), handler_(std::move(handler)){
    }
};

class websocket : public socket{

    // constants
    static constexpr auto CONNECTION_TIMEOUT_SECONDS = std::chrono::seconds{60};
    static const int MASK_SIZE_BYTES = 4;

public:

    // constructors and destructors
    websocket(std::shared_ptr<socket> socket, bool binary = true, bool server = true);
    websocket(boost::asio::io_service& io_service, bool binary = true, bool server = true);
    virtual ~websocket() override;

    // socket control
    void connect(const std::string &host, const std::string &port, std::chrono::seconds expire_seconds, ec_handler handler) override;
    void close() override;
    void cancel() override;
    void close(ec_handler handler);
    bool requires_handshake() const override;
    void run_handshake(ec_handler callback, const std::string& host) override;

    // read
    size_t read(uint8_t buffer[], size_t size, boost::system::error_code& ec) override;
    void async_read_some(uint8_t buffer[], size_t max_size, ec_size_handler handler) override;
    void async_write_some(ec_size_handler handler) override;
    void async_write_some(uint8_t buffer[], size_t size, ec_size_handler handler) override;
    void async_read(uint8_t buffer[], size_t size, ec_size_handler handler) override;
    void async_read(boost::asio::streambuf &buffer, size_t size, ec_size_handler handler) override;
    void async_read_until(boost::asio::streambuf& buffer, const boost::regex & expr, ec_size_handler handler) override;
    void async_read_until(boost::asio::streambuf &buffer, const std::string &delim, ec_size_handler handler) override;

    // write
    size_t write(uint8_t buffer[], size_t size, boost::system::error_code& ec) override;
    void async_write(uint8_t buffer[], size_t size, ec_size_handler handler) override;
    void async_write(const std::string& str, ec_size_handler handler) override;
    void async_write(const std::vector<boost::asio::const_buffer>& buffer, ec_size_handler handler) override;

    // wait
    void async_wait(boost::asio::socket_base::wait_type type, ec_handler handler) override;

    // some getters to check the state
    bool is_open() const override;
    bool is_secure() const override;
    size_t available() const override;
    std::string get_remote_ip() const override;
    std::string get_local_port() const override;
    std::string get_remote_port() const override;

    // other methods
    bool is_binary() const;
    void set_binary(bool binary);
    void handle_timeout();

private:
    // websocket internals
    void send_close(uint8_t buffer[], size_t size, ec_handler handler);
    void send_ping(uint8_t buffer[] = nullptr, size_t size=0, ec_handler handler={});
    void send_pong(uint8_t buffer[] = nullptr, size_t size=0, ec_handler handler={});

    // async payload write
    void handle_write(uint8_t buffer[], size_t size, ec_size_handler handler);
    void handle_write(uint8_t opcode, uint8_t buffer[], size_t size, ec_size_handler handler);
    void handle_pending_writes();

    // async payload read
    void handle_read(uint8_t buffer[], size_t max_size, ec_size_handler handler);
    void handle_payload(int opcode, bool fin,  uint8_t buffer[], std::size_t size, std::size_t max_size, ec_size_handler handler);
    void handle_payload_read(int opcode, bool fin, uint8_t buffer[], std::size_t size, bool masked, std::size_t max_size, ec_size_handler handler);

    // sync version of payload read
    std::size_t handle_payload_read(int opcode, bool fin, uint8_t buffer[], std::size_t size, bool masked, std::size_t max_size, boost::system::error_code& ec);
    std::size_t handle_payload(int opcode, bool fin, uint8_t buffer[], std::size_t size, std::size_t max_size, boost::system::error_code& ec);

    // payload unmasking
    void decode(uint8_t buffer[], size_t size, uint8_t mask[]);

private:

    std::shared_ptr<socket> socket_;
    boost::asio::deadline_timer timer_;
    bool binary_;
    bool server_role_;

    std::queue<pending_writes> pending_writes_;

    bool     new_message_           = true;
    size_t   current_size_          = 0;
    uint8_t  current_opcode_        = 0;

    uint8_t buffer_[140];
    uint8_t output_[14];
    uint8_t* source_buffer_ = nullptr;
    boost::asio::streambuf b;

    bool writing_        = false;
    bool close_received_ = false;
    bool close_sent_     = false;
    bool data_received_  = true;
    bool pending_ping_   = false;

};

}

#endif