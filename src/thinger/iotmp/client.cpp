#include "client.hpp"

namespace thinger::iotmp{

    client::client(std::string transport, bool shared_pool) :
            io_service_(shared_pool ? asio::workers.get_next_io_service() : asio::workers.get_isolated_io_service("iotmp client")),
            async_timer_(io_service_),
            transport_(std::move(transport))
    {
        if(transport_=="websocket"){
            socket_ = std::make_unique<thinger::asio::websocket>(io_service_, true, false);
        }else{
            auto ssl_ctx = std::make_shared<boost::asio::ssl::context>(boost::asio::ssl::context::sslv23_client);
            ssl_ctx->set_verify_mode(boost::asio::ssl::verify_none);
            socket_ = std::make_unique<thinger::asio::ssl_socket>("iotmp_client", io_service_, ssl_ctx);
        }
    }

    void client::set_credentials(const std::string& user, const std::string& device, const std::string& device_credential){
        username_ = user;
        device_id_ = device;
        device_password_ = device_credential;
    }

    void client::set_hostname(const std::string& host){
        hostname_ = host;
        iotmp::set_host(hostname_.c_str());
    }

    const std::string& client::get_hostname() const{
        return hostname_;
    }

    const std::string& client::get_user() const{
        return username_;
    }

    const std::string& client::get_device() const{
        return device_id_;
    }

    const std::string& client::get_credentials() const {
        return device_password_;
    }


    void client::start(std::function<void(exec_result)> callback){
        io_service_.dispatch([this, callback=std::move(callback)](){
            if(running_){
                if(callback) callback({false});
                return;
            }
            running_ = true;
            if(callback){
                callback({true});
            }
            LOG_INFO("starting ASIO client...");
            connect();
        });
    }

    void client::stop(std::function<void(exec_result)> callback){
        io_service_.dispatch([this, callback=std::move(callback)](){
            if(!running_){
                if(callback) callback({false});
                return;
            }
            running_ = false;
            if(callback) callback({true});
            LOG_INFO("stopping ASIO client...");
            disconnected();
        });
    }

    bool client::run(std::function<bool()> callback){
        std::promise<bool> p;
        io_service_.dispatch([this, &p, callback=std::move(callback)](){
            if(!connected()){
                p.set_value(false);
                return;
            }
            p.set_value(callback());
        });
        return p.get_future().get();
    }

    boost::asio::io_service& client::get_io_service(){
        return io_service_;
    }

    void client::authenticate(){
        if(iotmp::connect(username_.c_str(), device_id_.c_str(), device_password_.c_str())){
            notify_state(THINGER_AUTHENTICATED);

            // set authenticated flag
            authenticated_ = true;

            // read input
            wait_for_input();

            // start timer for streams & timeouts
            async_timer_.stop();
            async_timer_.set_timeout(std::chrono::seconds{1});
            async_timer_.set_callback([this]{
                auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                iotmp::handle(timestamp, false);
            });
            async_timer_.start();

            // subscribe to resources
            iotmp::initialize_streams();
        }else{
            notify_state(THINGER_AUTH_FAILED);
            disconnected();
        }
    }

    bool client::read(char* buffer, size_t size){
        boost::system::error_code ec;
        auto read = socket_->read((uint8_t *)buffer, size, ec);
        bool success = !ec && read==size;
        if(ec != boost::asio::error::operation_aborted && !success){
            notify_state(SOCKET_READ_ERROR, ec.message().c_str());
        }
        return success;
    }

    bool client::do_write(uint8_t* buffer, size_t size){
        if(size>0){
            boost::system::error_code ec;
            //LOG_F(INFO, "writing %zu (bytes)", size);
            auto write = socket_->write(buffer, size, ec);
            bool success = !ec && write==size;
            if(ec != boost::asio::error::operation_aborted && !success){
                notify_state(SOCKET_WRITE_ERROR, ec.message().c_str());
            }
            return success;
        }
        return true;
    }

    bool client::flush_out_buffer(){
        if(out_size_>0){
            bool success = do_write(out_buffer_, out_size_);
            out_size_ = 0;
            return success;
        }
        return true;
    }

    bool client::write(const char* buffer, size_t size, bool flush){
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

    void client::disconnected(){
        iotmp::disconnected();
        async_timer_.stop();
        socket_->close();
        out_size_ = 0;
        authenticated_ = false;
        if(running_ && !connecting_){
            async_timer_.set_timeout(std::chrono::seconds{THINGER_RECONNECT_SECONDS});
            LOG_INFO("tyring to connect again in %d seconds", THINGER_RECONNECT_SECONDS);
            async_timer_.set_callback([this]{
                // stop async timer
                async_timer_.stop();
                // try to connect
                connect();
            });
            async_timer_.start();
        }
    }

    void client::wait_for_input(){
        socket_->async_wait(boost::asio::ip::tcp::socket::wait_read, [this](const boost::system::error_code& ec){
            if(ec){
                if(ec != boost::asio::error::operation_aborted){
                    notify_state(SOCKET_READ_ERROR, ec.message().c_str());
                    return disconnected();
                }
                return; // just stop reading, the connection has been closed elsewhere
            }

            auto available = socket_->available();
            if(available){
                //LOG("received data: %zu", available);
                do{
                    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                    iotmp::handle(timestamp, true);
                }while(socket_->available());
                wait_for_input();
            }else{
                notify_state(SOCKET_READ_ERROR, "no available bytes...");
                disconnected();
            }
        });
    }

    void client::connect(){
        // set connecting to true
        connecting_ = true;

        notify_state(SOCKET_CONNECTING);

        std::string host = host_, port = std::to_string(port_);

        // modify host and port if connecting over a websocket transport
        if(transport_=="websocket"){
            // use wss or ws depending on ssl available
            host = std::string("wss://") + host_ + "/iotmp";
            // in websocket, port is used to specify upgrade protocol
            port = "iotmp";
        }

        socket_->connect(host, port, std::chrono::seconds{THINGER_CONNECT_TIMEOUT},[this](const boost::system::error_code& ec){
            // reset connecting flag
            connecting_ = false;

            // any error on connection ?
            if(ec){
                if(ec != boost::asio::error::operation_aborted){
                    return disconnected();
                }
                return; // just stop here, the connection has been closed elsewhere
            }

            notify_state(SOCKET_CONNECTED);
            notify_state(THINGER_AUTHENTICATING);
            authenticate();
        });
    }

    bool client::connected() const{
        return socket_->is_open() && authenticated_;
    }
}