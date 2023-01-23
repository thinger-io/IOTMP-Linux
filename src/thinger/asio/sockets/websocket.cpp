#include "websocket.hpp"

#include "../../http/util/utf8.hpp"
#include "../../http/http_request.hpp"
#include "../../http/http_client.hpp"

#include <random>
#include <boost/iostreams/copy.hpp>
#include <boost/iostreams/filter/zlib.hpp>
#include <boost/iostreams/device/file.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/detail/config/zlib.hpp>

/*
#include <gzip.h>
#include <zlib.h>
 */

namespace thinger::asio{

    static constexpr size_t MAX_PAYLOAD_SIZE = 1*1024*1024;

    using random_bytes_engine = std::independent_bits_engine<std::default_random_engine, CHAR_BIT, unsigned char>;

    /*
    std::string compress(const std::string& data)
    {
        std::string compressed;

        CryptoPP::ZlibCompressor zipper(new CryptoPP::StringSink(compressed));
        zipper.Put((unsigned char*) data.data(), data.size());
        zipper.MessageEnd();
        return compressed;
    }

    std::string decompress(const std::string& data)
    {
        std::string decompressed;
        CryptoPP::ZlibDecompressor unzipper(new CryptoPP::StringSink(decompressed));
        unzipper.Put( (unsigned char*) data.data(), data.size());
        unzipper.MessageEnd();
        return decompressed;
    }
    */

    websocket::websocket(std::shared_ptr<socket> socket, bool binary, bool server) :
        asio::socket("websocket", socket->get_io_service()),
        socket_(socket),
        timer_(socket->get_io_service()),
        binary_(binary),
        server_role_(server)
    {
        LOG_LEVEL(2, "websocket created");
    }

    websocket::websocket(boost::asio::io_service& io_service, bool binary, bool server) :
            asio::socket("websocket", io_service),
            timer_(io_service),
            binary_(binary),
            server_role_(server)
    {
        LOG_LEVEL(2, "websocket created");
    }

    websocket::~websocket()
    {
        LOG_LEVEL(2, "releasing websocket");
        boost::system::error_code ec;
        timer_.cancel(ec);
    }

    void websocket::decode(uint8_t buffer[], std::size_t size, uint8_t mask[]){
        LOG_LEVEL(3, "decoding payload. size: %zu bytes masked: %d", size, mask!=nullptr);
        LOG_LEVEL(3, "applying mask on input data: 0x%02X%02X%02X%02X", mask[0], mask[1], mask[2], mask[3]);
        for(auto i=0; i<size; ++i){
            buffer[i] ^= mask[i%MASK_SIZE_BYTES];
        }
    }

    void websocket::handle_timeout(){
        boost::system::error_code ec;
        timer_.expires_from_now(boost::posix_time::seconds{CONNECTION_TIMEOUT_SECONDS.count()}, ec);
        timer_.async_wait([this](const boost::system::error_code &e){
            if(e){
                if(e!=boost::asio::error::operation_aborted){
                    LOG_ERROR("error on timeout: %s", e.message().c_str());
                }
                return;
            }
            // if data received, reset flag and wait for next update
            if(data_received_){
                data_received_ = false;
                return handle_timeout();

            // no data in the last minute, just send a ping
            } else{
                // 60 seconds without data, send a ping
                if(!pending_ping_){
                    pending_ping_ = true;
                    // send ping
                    send_ping(nullptr, 0, [this](const boost::system::error_code& e){
                        if(e){
                            close();
                        }else{
                            handle_timeout();
                        }
                    });
                // already send a ping, and still no data... just close connection
                }else{
                    LOG_LEVEL(3, "websocket ping timeout... closing connection!");
                    close();
                }
            }
        });
    }

    void websocket::send_close(uint8_t buffer[], size_t size, ec_handler handler){
        LOG_LEVEL(3, "sending close frame");
        handle_write(0x88, buffer, size, [this, handler=std::move(handler)](const boost::system::error_code& e, std::size_t bytes_transferred){
            if(e){
                close();
                return handler(e);
            }
            close_sent_ = true;
            LOG_LEVEL(2, "close frame sent");
            if(close_received_){
                LOG_LEVEL(2, "close sent, connection disposed");
                close();
                handler(e);
            }else{
                boost::system::error_code ec;
                timer_.cancel();
                timer_.expires_from_now(boost::posix_time::seconds{5}, ec);
                timer_.async_wait([this, handler=std::move(handler)](const boost::system::error_code &e){
                    if(e){
                        if(e!=boost::asio::error::operation_aborted){
                            LOG_ERROR("error on timeout: %s", e.message().c_str());
                        }
                        return;
                    }
                    if(!close_received_){
                        LOG_WARNING("timeout while waiting close acknowledgement");
                    }
                    close();
                    handler(e);
                });
            }
        });
    }

    void websocket::send_ping(uint8_t buffer[], size_t size, ec_handler handler){
        LOG_LEVEL(3, "sending ping frame");
        handle_write(0x09, buffer, size, [handler=std::move(handler)](const boost::system::error_code& e, std::size_t bytes_transferred){{
            LOG_LEVEL(3, "ping frame sent");
            if(handler) handler(e);
        }});
    }

    void websocket::send_pong(uint8_t buffer[], size_t size, ec_handler handler){
        LOG_LEVEL(3, "sending pong frame");
        handle_write(0x0A, buffer, size, [handler=std::move(handler)](const boost::system::error_code& e, std::size_t bytes_transferred){{
            LOG_LEVEL(3, "pong frame sent");
            if(handler) handler(e);
        }});
    }

    void websocket::handle_payload(
            int opcode,
            bool fin,
            uint8_t buffer[],
            size_t size,
            size_t max_size,
            std::function<void(const boost::system::error_code&, std::size_t)> handler)
    {

        // if it is a control frame
        if(opcode>=0x8){
            // control frames are handled in a single message, so, not use current_opcode_ that is only
            // used for potential text and binary fragmented messages
            switch(opcode){
                case 0x8:     // connection close
                    LOG_LEVEL(2, "received close frame");
                    close_received_ = true;
                    if(close_sent_){
                        LOG_LEVEL(2, "close already sent, stopping client");
                        // notify handler that we will not read anything else
                        return handler(boost::asio::error::connection_aborted, 0);
                    }else{
                        // gracefully close the connection
                        return send_close(nullptr, 0, [handler = std::move(handler)](const boost::system::error_code& ec){
                            // notify handler that we will not read anything else
                            handler(boost::asio::error::connection_aborted, 0);
                        });
                    }
                case 0x9:     // ping
                    LOG_LEVEL(2, "received ping frame");
                    return send_pong(buffer, size, [this, buffer, max_size, handler=std::move(handler)](const boost::system::error_code& ec){
                        // keep reading data for the handler
                        if(!ec) handle_read(buffer, max_size, std::move(handler));
                    });
                case 0xA:   // pong
                    LOG_LEVEL(2, "received pong frame");
                    // clear pending ping, so nex timeout can issue a new ping instead of expire
                    pending_ping_ = false;
                    // do not compute pong as "data", so ping starts again next timer iteration
                    data_received_ = false;
                    // keep reading data for the handler
                    return handle_read(buffer, max_size, std::move(handler));
                default:
                    LOG_ERROR("unexpected opcode: %d", (int) opcode);
                    return handler(boost::asio::error::invalid_argument, 0);
            }
        }else{
            // update current reading size (for framing support on non-control messages)
            current_size_ += size;

            /**
             * this is the last fn when handling a websocket input message, any path should handle another
             * handle_read with the provided handler, or call the handler with the actual data read
             */

            if(!fin){
                // ensure continuation frames comes with opcode 0
                if(!new_message_ && opcode!=0x0){
                    LOG_INFO("not FIN with a different opcode: %u", opcode);
                    // notify waiting listener
                    return handler(boost::asio::error::invalid_argument, 0);
                }

                // clear new message flag, so, next handle_read takes it into account
                new_message_ = false;

                // not finished message with opcode 0x0 -> keep reading current message
                return handle_read(buffer+size, max_size-size, std::move(handler));
            }else{
                // finished reading the message! check source opcode
                switch(current_opcode_){
                    case 0x1:     // text frame
                        LOG_LEVEL(2, "received text frame");
                        new_message_ = true;
                        if(utf8_naive(source_buffer_, current_size_)==0){
                            return handler(boost::system::error_code{}, current_size_);
                        }else{
                            LOG_ERROR("invalid UTF8 message received!");
                            // notify waiting listener
                            return handler(boost::asio::error::invalid_argument, 0);
                        }
                    case 0x2:     // binary frame
                        LOG_LEVEL(2, "received binary frame");
                        new_message_ = true;
                        return handler(boost::system::error_code{}, current_size_);
                    default:    // future control frames
                        LOG_ERROR("received unknown websocket opcode: %d", (int) opcode);
                        return handler(boost::asio::error::invalid_argument, 0);
                }
            }
        }
    }

    std::size_t websocket::handle_payload(
            int opcode,
            bool fin,
            uint8_t buffer[],
            size_t size,
            size_t max_size,
            boost::system::error_code& ec)
    {

        // if it is a control frame
        if(opcode>=0x8){
            // control frames are handled in a single message, so, not use current_opcode_ that is only
            // used for potential text and binary fragmented messages
            switch(opcode){
                case 0x8:     // connection close
                    LOG_LEVEL(2, "received close frame");
                    close_received_ = true;
                    if(close_sent_){
                        LOG_LEVEL(2, "close already sent, stopping client");
                        // notify handler that we will not read anything else
                        ec = boost::asio::error::connection_aborted;
                        return 0;
                    }else{
                        // gracefully close the connection
                        send_close(nullptr, 0, [](const boost::system::error_code& ec){

                        });
                        ec = boost::asio::error::connection_aborted;
                        return 0;
                    }
                case 0x9:     // ping
                    LOG_LEVEL(2, "received ping frame");
                    send_pong(buffer, size, [](const boost::system::error_code& ec){

                    });
                    return read(buffer, max_size, ec);
                case 0xA:   // pong
                    LOG_LEVEL(2, "received pong frame");
                    // clear pending ping, so nex timeout can issue a new ping instead of expire
                    pending_ping_ = false;
                    // do not compute pong as "data", so ping starts again next timer iteration
                    data_received_ = false;
                    // keep reading data for the handler
                    return read(buffer, max_size, ec);
                default:
                    LOG_ERROR("unexpected opcode: %d", (int) opcode);
                    ec = boost::asio::error::invalid_argument;
                    return 0;
            }
        }else{
            // update current reading size (for framing support on non-control messages)
            current_size_ += size;

            /**
             * this is the last fn when handling a websocket input message, any path should handle another
             * handle_read with the provided handler, or call the handler with the actual data read
             */

            if(!fin){
                // ensure continuation frames comes with opcode 0
                if(!new_message_ && opcode!=0x0){
                    LOG_INFO("not FIN with a different opcode: %u", opcode);
                    // notify waiting listener
                    ec = boost::asio::error::invalid_argument;
                    return 0;
                }

                // clear new message flag, so, next handle_read takes it into account
                new_message_ = false;

                // not finished message with opcode 0x0 -> keep reading current message
                return read(buffer+size, max_size-size, ec);
            }else{
                // finished reading the message! check source opcode
                switch(current_opcode_){
                    case 0x1:     // text frame
                        LOG_LEVEL(2, "received text frame");
                        new_message_ = true;
                        if(utf8_naive(source_buffer_, current_size_)==0){
                            ec = boost::system::error_code{};
                            return current_size_;
                        }else{
                            LOG_INFO("invalid UTF8 message received!");
                            // notify waiting listener
                            ec = boost::asio::error::invalid_argument;
                            return 0;
                        }
                    case 0x2:     // binary frame
                        LOG_LEVEL(2, "received binary frame");
                        new_message_ = true;
                        ec = boost::system::error_code{};
                        return current_size_;
                    default:    // future control frames
                        LOG_ERROR("received unknown websocket opcode: %d", (int) opcode);
                        ec = boost::asio::error::invalid_argument;
                        return current_size_;
                }
            }
        }
    }

    void websocket::handle_payload_read(
            int opcode,
            bool fin,
            uint8_t buffer[],
            size_t size,
            bool masked,
            size_t max_size,
            ec_size_handler handler)
    {
        if(!masked){
            LOG_LEVEL(3, "reading %zu bytes of unmasked content", size);
            socket_->async_read(buffer, size, [this, opcode, fin, buffer, size, max_size, handler=std::move(handler)](const boost::system::error_code& e, std::size_t bytes_transferred){
                if(e){
                    //LOG_ERROR("error while reading unmasked content: %s", e.message().c_str());
                    return handler(e, 0);
                }
                handle_payload(opcode, fin, buffer, size, max_size, std::move(handler));
            });
        }else{
            LOG_LEVEL(3, "reading mask: %u", MASK_SIZE_BYTES);
            socket_->async_read(buffer_, MASK_SIZE_BYTES, [this, opcode, fin, buffer, size, max_size, handler=std::move(handler)](const boost::system::error_code& e, std::size_t bytes_transferred){
                if(e){
                    //LOG_ERROR("error while reading mask: %s", e.message().c_str());
                    return handler(e, 0);
                }
                LOG_LEVEL(3, "mask read: %zu bytes", bytes_transferred);

                LOG_LEVEL(3, "reading payload: %zu bytes", size);
                socket_->async_read(buffer, size, [this, opcode, fin, buffer, size, max_size, handler=std::move(handler)](const boost::system::error_code& e, std::size_t bytes_transferred){
                    if(e){
                        //LOG_ERROR("error while reading payload: %s", e.message().c_str());
                        return handler(e, 0);
                    }

                    LOG_LEVEL(3, "payload read: %zu bytes", bytes_transferred);

                    // decode payload with mask read
                    decode(buffer, size, buffer_);

                    handle_payload(opcode, fin, buffer, size, max_size, std::move(handler));
                });
            });
        }
    }

    std::size_t websocket::handle_payload_read(
            int opcode,
            bool fin,
            uint8_t buffer[],
            size_t size,
            bool masked,
            size_t max_size,
            boost::system::error_code& ec)
    {
        if(!masked){
            LOG_LEVEL(3, "reading %zu bytes of unmasked content", size);
            auto bytes_transferred = socket_->read(buffer, size, ec);
            if(ec) return 0;
            return handle_payload(opcode, fin, buffer, size, max_size, ec);
        }else{
            LOG_LEVEL(3, "reading mask: %u", MASK_SIZE_BYTES);
            auto bytes_transferred = socket_->read(buffer_, MASK_SIZE_BYTES, ec);
            if(ec || bytes_transferred!=MASK_SIZE_BYTES) return 0;
            LOG_LEVEL(3, "mask read: %zu bytes", bytes_transferred);
            LOG_LEVEL(3, "reading payload: %zu bytes", size);
            bytes_transferred = socket_->read(buffer, size, ec);
            if(ec || bytes_transferred!=size) return 0;
            // decode payload with mask read
            decode(buffer, size, buffer_);
            return handle_payload(opcode, fin, buffer, size, max_size, ec);
        }
    }

    void websocket::handle_read(uint8_t buffer[], size_t max_size, ec_size_handler handler){
        LOG_LEVEL(3, "waiting websocket frame header, socket: %d, connected: %d", socket_!=nullptr, socket_->is_open());
        socket_->async_read(buffer_, 2, [this, buffer, max_size, handler](const boost::system::error_code& e, std::size_t bytes_transferred){

            // if there is any error, then stop the connection
            if(e){
                /*
                if(e!=boost::asio::error::operation_aborted){
                    LOG_ERROR("websocket read error: %s", e.message().c_str());
                }
                 */
                return handler(e, 0);
            }

            LOG_LEVEL(3, "socket read: %zu bytes", bytes_transferred);

            data_received_ = true;

            /*
             WebSocket header
                0                   1                   2                   3
              0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
             +-+-+-+-+-------+-+-------------+-------------------------------+
             |F|R|R|R| opcode|M| Payload len |    Extended payload length    |
             |I|S|S|S|  (4)  |A|     (7)     |             (16/64)           |
             |N|V|V|V|       |S|             |   (if payload len==126/127)   |
             | |1|2|3|       |K|             |                               |
             +-+-+-+-+-------+-+-------------+ - - - - - - - - - - - - - - - +
             |     Extended payload length continued, if payload len == 127  |
             + - - - - - - - - - - - - - - - +-------------------------------+
             |                               |Masking-key, if MASK set to 1  |
             +-------------------------------+-------------------------------+
             | Masking-key (continued)       |          Payload Data         |
             +-------------------------------- - - - - - - - - - - - - - - - +
             :                     Payload Data continued ...                :
             + - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - +
             |                     Payload Data continued ...                |
             +---------------------------------------------------------------+
            */

            uint8_t data_type = buffer_[0];
            uint8_t fin = data_type & 0b10000000;        // get fin
            uint8_t rsv = data_type & 0b01110000;        // get rsv

            // no protocol extensions supported
            if(rsv){
                LOG_ERROR("invalid RSV parameters");
                return handler(boost::asio::error::invalid_argument, 0);
            }

            uint8_t opcode = data_type & (uint8_t) 0x0F;           // get opcode
            uint8_t data_size =  buffer_[1] & (uint8_t) ~(1 << 7);
            uint8_t masked = buffer_[1] & (uint8_t) 0b10000000;

            LOG_LEVEL(3, "decode frame header. fin: %d, opcode: 0x%02X mask: %d data_size: %d", fin, opcode, masked, data_size);

            // ensure clients are masking the information
            if(!masked && server_role_){
                LOG_ERROR("client is not masking the information");
                return handler(boost::asio::error::invalid_argument, 0);
            }

            // reset variables in every new message
            if(new_message_){

                // reset current size
                current_size_ = 0;

                switch(opcode){
                    case 0x0:     // continuation frame
                        LOG_ERROR("received continuation message as the first message!");
                        return handler(boost::asio::error::invalid_argument, 0);
                    case 0x1:   // text frame
                        current_opcode_ = opcode;
                        source_buffer_ = buffer;
                        break;
                    case 0x2:   // binary frame
                        current_opcode_ = opcode;
                        break;
                    case 0x8:   // connection close
                    case 0x9:   // ping
                    case 0xA:   // pong
                        if(!fin){
                            LOG_ERROR("control frame messages cannot be fragmented");
                            return handler(boost::asio::error::invalid_argument, 0);
                        }
                        break;
                    default:    // future control frames
                        LOG_ERROR("received unknown websocket opcode: %d", (int) opcode);
                        return handler(boost::asio::error::invalid_argument, 0);
                }
            }else{
                // not a new message? a fragmented read is in process, only accept continuation frames
                // or control messages
                switch(opcode){
                    case 0x0:    // continuation frame (expected case)
                        LOG_LEVEL(3, "received continuation frame, current size is: %zu", current_size_);
                        break;
                    case 0x1:   // text frame
                    case 0x2:   // binary frame
                        LOG_LEVEL(3, "received control frame in the middle of a message, current size is: %zu", current_size_);
                        LOG_ERROR("unexpected fragment type. expecting a continuation frame");
                        return handler(boost::asio::error::invalid_argument, 0);
                    case 0x8:   // connection close
                    case 0x9:   // ping
                    case 0xA:   // pong
                        if(!fin){
                            LOG_ERROR("control frame messages cannot be fragmented");
                            return handler(boost::asio::error::invalid_argument, 0);
                        }
                        break;
                    default:    // future control frames
                        LOG_ERROR("received unknown websocket opcode: %d", (int) opcode);
                        return handler(boost::asio::error::invalid_argument, 0);
                }
            }


            /**
             *
              Payload length:  7 bits, 7+16 bits, or 7+64 bits

              The length of the "Payload data", in bytes: if 0-125, that is the
              payload length.  If 126, the following 2 bytes interpreted as a
              16-bit unsigned integer are the payload length.  If 127, the
              following 8 bytes interpreted as a 64-bit unsigned integer (the
              most significant bit MUST be 0) are the payload length.  Multibyte
              length quantities are expressed in network byte order.  Note that
              in all cases, the minimal number of bytes MUST be used to encode
              the length, for example, the length of a 124-byte-long string
              can't be encoded as the sequence 126, 0, 124.  The payload length
              is the length of the "Extension data" + the length of the
              "Application data".  The length of the "Extension data" may be
              zero, in which case the payload length is the length of the
              "Application data".
             */

            // we know the current size, so read it directly
            if(data_size<126){
                // ensure buffer capacity
                if(data_size>max_size){
                    LOG_ERROR("websocket data is bigger than buffer, max buffer size: %zu", max_size);
                    return handler(boost::asio::error::no_buffer_space, 0);
                }
                // note: even payload of 0 should be read, as it may contain a mask
                handle_payload_read(opcode, fin, buffer, data_size, masked, max_size, std::move(handler));
            }else{

                // ensure PING, PONG, OR CLOSE, does not send messages with more than 125 characters
                if(opcode>=0x8){
                    LOG_ERROR("control frame payload cannot exceed 125 bytes");
                    return handler(boost::asio::error::invalid_argument, 0);
                }

                // we have to read the number of bytes
                uint8_t bytes_size;
                if(data_size==126) bytes_size = 2;
                else if(data_size==127) bytes_size = 8;
                else{
                    LOG_ERROR("invalid websocket frame size");
                    return handler(boost::asio::error::invalid_argument, 0);
                }

                LOG_LEVEL(3, "reading %d additional bytes for getting payload size", bytes_size);
                socket_->async_read(buffer_, bytes_size, [this, opcode, fin, buffer, masked, max_size, handler = std::move(handler)](const boost::system::error_code& e, std::size_t bytes_transferred){
                    LOG_LEVEL(3, "read payload size: %zu", bytes_transferred);
                    if(e){
                        /*
                        if(e!=boost::asio::error::operation_aborted){
                            LOG_ERROR("websocket read error: %s", e.message().c_str());
                        }*/
                        return handler(e, 0);
                    }

                    LOG_LEVEL(3, "socket read: %zu bytes", bytes_transferred);
                    uint64_t size = 0;
                    for(auto i=0; i<bytes_transferred; ++i) size = (size << 8) + buffer_[i];
                    // ensure buffer capacity
                    if(size>max_size){
                        LOG_ERROR("websocket data is bigger than buffer, max buffer size: %zu", max_size);
                        return handler(boost::asio::error::no_buffer_space, 0);
                    }
                    handle_payload_read(opcode, fin, buffer, size, masked, max_size, std::move(handler));
                });
            }
        });
    }

    void websocket::async_read_some(uint8_t buffer[], size_t max_size, ec_size_handler handler)
    {
        handle_read(buffer, max_size, std::move(handler));
    }

    void websocket::handle_pending_writes(){
        if(!pending_writes_.empty()){
            auto pending = std::move(pending_writes_.front());
            pending_writes_.pop();
            handle_write(pending.buffer_, pending.size_, std::move(pending.handler_));
        }
    }

    void websocket::handle_write(uint8_t opcode, uint8_t buffer[], size_t size, ec_size_handler handler){
        // do not accept async write while the socket is writing, it will be queued once current writing is done
        if(writing_){
            LOG_LEVEL(3, "adding write to queue. current queue size: %zu", pending_writes_.size());
            pending_writes_.emplace(buffer, size, std::move(handler));
            return;
        }

        writing_ = true;

        /*
         WebSocket header
            0                   1                   2                   3
          0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
         +-+-+-+-+-------+-+-------------+-------------------------------+
         |F|R|R|R| opcode|M| Payload len |    Extended payload length    |
         |I|S|S|S|  (4)  |A|     (7)     |             (16/64)           |
         |N|V|V|V|       |S|             |   (if payload len==126/127)   |
         | |1|2|3|       |K|             |                               |
         +-+-+-+-+-------+-+-------------+ - - - - - - - - - - - - - - - +
         |     Extended payload length continued, if payload len == 127  |
         + - - - - - - - - - - - - - - - +-------------------------------+
         |                               |Masking-key, if MASK set to 1  |
         +-------------------------------+-------------------------------+
         | Masking-key (continued)       |          Payload Data         |
         +-------------------------------- - - - - - - - - - - - - - - - +
         :                     Payload Data continued ...                :
         + - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - +
         |                     Payload Data continued ...                |
         +---------------------------------------------------------------+
        */

        // build a header for the out data
        uint8_t header_size = 2;
        output_[0] = 0x80 | opcode; // FIN and opcode = 1 (binary frame)

        // header payload
        if(size <=125){
            output_[1] = size;
        }else if(size >=126 && size <=65535){
            output_[1] = 126;
            for(auto i=1; i<=2; ++i) output_[i+1] = (size >> (2-i)*8 ) & 0xff;
            header_size += 2;
        }else if(size >65535){
            output_[1] = 127;
            for(auto i=1; i<=8; ++i) output_[i+1] = (size >> (8-i)*8 ) & 0xff;
            header_size += 8;
        }

        // mask data if required
        if(!server_role_){ // if it is a client, all writes must be masked
            output_[1] |= 0b10000000; // masked flag

            // initialize masked information on header
            static random_bytes_engine rbe;
            std::vector<uint8_t> rnd(MASK_SIZE_BYTES);
            std::generate(begin(rnd), end(rnd), std::ref(rbe));

            for(auto i=0; i<MASK_SIZE_BYTES; ++i){
                output_[header_size+i] = rnd[i];
            }

            // update header size
            header_size += MASK_SIZE_BYTES;

            // apply masking information
            LOG_LEVEL(3, "applying mask on output message: 0x%02X%02X%02X%02X", rnd[0], rnd[1], rnd[2], rnd[3]);
            for(auto i=0; i<size; ++i){
                buffer[i] ^= rnd[i%MASK_SIZE_BYTES];
            }
        }

        LOG_LEVEL(3, "sending websocket data. header: %u, payload: %zu, total: %zu", header_size, size, header_size+size);

        // create buffer with header + payload
        std::vector<boost::asio::const_buffer> output_buffer;
        output_buffer.push_back(boost::asio::const_buffer(output_, header_size));
        output_buffer.push_back(boost::asio::const_buffer(buffer, size));

        socket_->async_write(output_buffer, [this, header_size, handler = std::move(handler)](const boost::system::error_code& e, std::size_t bytes_transferred){
            writing_ = false;

            // if there is any error, then stop the connection
            if(e){
                /*
                if(e!=boost::asio::error::operation_aborted){
                    LOG_ERROR("error while writing to websocket: %s", e.message().c_str());
                }*/
                return handler(e, bytes_transferred);
            }

            LOG_LEVEL(3, "bytes sent %zu", bytes_transferred);

            // resolve current write callback
            handler(e, bytes_transferred-header_size);

            // run pending writes
            handle_pending_writes();
        });
    }

    void websocket::handle_write(uint8_t buffer[], size_t size, ec_size_handler handler){
        handle_write(binary_ ? 0x02 : 0x01, buffer, size, std::move(handler));
    }

    void websocket::close() {
        // cancel timer
        timer_.cancel();
        // close socket
        if(socket_) socket_->close();
        // clear any pending writes (basically call all callbacks)
        while(!pending_writes_.empty()){
            auto pending = std::move(pending_writes_.front());
            pending_writes_.pop();
            pending.handler_(boost::asio::error::operation_aborted, 0);
        }
    }

    void websocket::close(ec_handler handler) {
        if(!close_sent_){
            if(socket_->is_open()) {
                return send_close(nullptr, 0, [this, handler = std::move(handler)](const boost::system::error_code &e) {
                    handler(e);
                });
            }
        }else if(socket_->is_open()){
            close();
        }
        handler(boost::system::error_code{});
    }

    bool websocket::requires_handshake() const{
        return socket_ && socket_->requires_handshake();
    }

    void websocket::run_handshake(std::function<void(const boost::system::error_code& error)> callback, const std::string& host){
        if(socket_){
            socket_->run_handshake(std::move(callback), host);
        }else{
            callback(boost::asio::error::not_connected);
        }
    }

    void websocket::async_write_some(ec_size_handler handler)
    {
        handler(boost::asio::error::operation_not_supported, 0);
    }

    void websocket::async_write_some(uint8_t buffer[], size_t size, ec_size_handler handler)
    {
        handler(boost::asio::error::operation_not_supported, 0);
    }

    void websocket::async_read(uint8_t buffer[], size_t size, ec_size_handler handler)
    {
        handle_read(buffer, size, std::move(handler));
    }

    void websocket::async_read(boost::asio::streambuf &buffer, size_t size,
                               ec_size_handler handler) {
        handler(boost::asio::error::operation_not_supported, 0);
    }

    void websocket::async_read_until(boost::asio::streambuf& buffer, const boost::regex & expr, ec_size_handler handler)
    {
        handler(boost::asio::error::operation_not_supported, 0);
    }

    void websocket::async_read_until(boost::asio::streambuf &buffer, const std::string &delim, ec_size_handler handler){
        handler(boost::asio::error::operation_not_supported, 0);
    }

    void websocket::async_write(uint8_t buffer[], size_t size, ec_size_handler handler)
    {
        handle_write(buffer, size, std::move(handler));
    }

    void websocket::async_write(const std::string& str, ec_size_handler handler)
    {
        handle_write((uint8_t *) str.c_str(), str.size(), std::move(handler));
    }

    void websocket::async_write(const std::vector<boost::asio::const_buffer>& buffer, ec_size_handler handler)
    {
        handler(boost::asio::error::operation_not_supported, 0);
    }

    void websocket::connect(const std::string &url, const std::string &protocol, std::chrono::seconds expire_seconds, ec_handler handler){
        auto request = http::http_request::create_http_request(http::method::GET, url);
        if(!protocol.empty()){
            request->add_header("Sec-WebSocket-Protocol", protocol);
        }
        http::send_request(io_service_, request,
           [this, handler=std::move(handler)](const boost::system::error_code& error,
                  std::shared_ptr<http::http_client_connection> connection,
                  std::shared_ptr<http::http_response> response){
               if(error){
                   return handler(error);
               }else if(response && response->get_status()==http::http_response::status_type::switching_protocols){
                   socket_ = connection->release_socket();
                   return handler(boost::system::error_code{});
               }else{
                   return handler(boost::asio::error::host_not_found);
               }
           });
    }

    bool websocket::is_open() const{
        return socket_ && socket_->is_open();
    }

    bool websocket::is_secure() const{
        return socket_->is_secure();
    }

    std::string websocket::get_remote_ip() const {
        return std::static_pointer_cast<tcp_socket>(socket_)->get_remote_ip();
    }

    std::string websocket::get_local_port() const {
        return std::static_pointer_cast<tcp_socket>(socket_)->get_local_port();
    }

    std::string websocket::get_remote_port() const {
        return std::static_pointer_cast<tcp_socket>(socket_)->get_remote_port();
    }

    size_t websocket::read(uint8_t buffer[], size_t size, boost::system::error_code& ec){

        // is there enough data in buffer ?
        if(b.size()>=size){
            b.sgetn(reinterpret_cast<char *>(buffer), size);
            return size;
        }

        auto bytes_transferred = socket_->read(buffer_, 2, ec);
        if(ec) return 0;

        LOG_LEVEL(3, "socket read: %zu bytes", bytes_transferred);

        data_received_ = true;

        uint8_t data_type = buffer_[0];
        uint8_t fin = data_type & 0b10000000;        // get fin
        uint8_t rsv = data_type & 0b01110000;        // get rsv

        // no protocol extensions supported
        if(rsv){
            LOG_ERROR("invalid RSV parameters");
            ec = boost::asio::error::invalid_argument;
            return 0;
        }

        uint8_t opcode = data_type & (uint8_t) 0x0F;           // get opcode
        uint8_t data_size =  buffer_[1] & (uint8_t) ~(1 << 7);
        uint8_t masked = buffer_[1] & (uint8_t) 0b10000000;

        LOG_LEVEL(3, "decode frame header. fin: %d, opcode: 0x%02X mask: %d data_size: %d", fin, opcode, masked, data_size);

        // ensure clients are masking the information
        if(!masked && server_role_){
            LOG_ERROR("client is not masking the information");
            ec = boost::asio::error::invalid_argument;
            return 0;
        }

        // reset variables in every new message
        if(new_message_){

            // reset current size
            current_size_ = 0;

            switch(opcode){
                case 0x0:     // continuation frame
                    LOG_ERROR("received continuation message as the first message!");
                    ec = boost::asio::error::invalid_argument;
                    return 0;
                case 0x1:   // text frame
                    current_opcode_ = opcode;
                    source_buffer_ = buffer;
                    break;
                case 0x2:   // binary frame
                    current_opcode_ = opcode;
                    break;
                case 0x8:   // connection close
                case 0x9:   // ping
                case 0xA:   // pong
                    if(!fin){
                        LOG_ERROR("control frame messages cannot be fragmented");
                        ec = boost::asio::error::invalid_argument;
                        return 0;
                    }
                    break;
                default:    // future control frames
                    LOG_ERROR("received unknown websocket opcode: %d", (int) opcode);
                    ec = boost::asio::error::invalid_argument;
                    return 0;
            }
        }else{
            // not a new message? a fragmented read is in process, only accept continuation frames
            // or control messages
            switch(opcode){
                case 0x0:    // continuation frame (expected case)
                    LOG_LEVEL(3, "received continuation frame, current size is: %zu", current_size_);
                    break;
                case 0x1:   // text frame
                case 0x2:   // binary frame
                    LOG_LEVEL(3, "received control frame in the middle of a message, current size is: %zu", current_size_);
                    LOG_ERROR("unexpected fragment type. expecting a continuation frame");
                    ec = boost::asio::error::invalid_argument;
                    return 0;
                case 0x8:   // connection close
                case 0x9:   // ping
                case 0xA:   // pong
                    if(!fin){
                        LOG_ERROR("control frame messages cannot be fragmented");
                        ec = boost::asio::error::invalid_argument;
                        return 0;
                    }
                    break;
                default:    // future control frames
                    LOG_ERROR("received unknown websocket opcode: %d", (int) opcode);
                    ec = boost::asio::error::invalid_argument;
                    return 0;
            }
        }

        std::vector<uint8_t> temp_buffer;

        // we know the current size, so read it directly
        if(data_size<126){
            // reserve buffer size
            temp_buffer.resize(data_size);

            // note: even payload of 0 should be read, as it may contain a mask
            auto bytes_read = handle_payload_read(opcode, fin, temp_buffer.data(), data_size, masked, MAX_PAYLOAD_SIZE, ec);
            if(ec) return 0;
            b.sputn((const char*)temp_buffer.data(), bytes_read);
        }else{
            // ensure PING, PONG, OR CLOSE, does not send messages with more than 125 characters
            if(opcode>=0x8){
                LOG_ERROR("control frame payload cannot exceed 125 bytes");
                ec = boost::asio::error::invalid_argument;
                return 0;
            }

            // we have to read the number of bytes
            uint8_t bytes_size;
            if(data_size==126) bytes_size = 2;
            else if(data_size==127) bytes_size = 8;
            else{
                LOG_ERROR("invalid websocket frame size");
                ec = boost::asio::error::invalid_argument;
                return 0;
            }

            bytes_transferred = socket_->read(buffer_, bytes_size, ec);
            if(ec) return 0;

            LOG_LEVEL(3, "socket read: %zu bytes", bytes_transferred);
            uint64_t size = 0;
            for(auto i=0; i<bytes_transferred; ++i) size = (size << 8) + buffer_[i];

            // ensure buffer capacity
            if(size>MAX_PAYLOAD_SIZE){
                LOG_ERROR("websocket data is bigger than buffer, max buffer size: %zu", MAX_PAYLOAD_SIZE);
                ec = boost::asio::error::no_buffer_space;
                return 0;
            }

            // reserve buffer size
            temp_buffer.resize(size);
            auto bytes_read = handle_payload_read(opcode, fin, temp_buffer.data(), size, masked, MAX_PAYLOAD_SIZE, ec);
            if(ec) return 0;
            b.sputn((const char*)temp_buffer.data(), bytes_read);
        }

        if(b.size()>=size){
            b.sgetn(reinterpret_cast<char *>(buffer), size);
            return size;
        }

        return 0;
    }

    size_t websocket::write(uint8_t buffer[], size_t size, boost::system::error_code& ec){
        auto opcode = binary_ ? 0x02 : 0x01;

        // build a header for the out data
        uint8_t header_size = 2;
        output_[0] = 0x80 | opcode; // FIN and opcode = 1 (binary frame)

        // header payload
        if(size <=125){
            output_[1] = size;
        }else if(size >=126 && size <=65535){
            output_[1] = 126;
            for(auto i=1; i<=2; ++i) output_[i+1] = (size >> (2-i)*8 ) & 0xff;
            header_size += 2;
        }else if(size >65535){
            output_[1] = 127;
            for(auto i=1; i<=8; ++i) output_[i+1] = (size >> (8-i)*8 ) & 0xff;
            header_size += 8;
        }

        // mask data if required
        if(!server_role_){ // if it is a client, all writes must be masked
            output_[1] |= 0b10000000; // masked flag

            // initialize masked information on header
            static random_bytes_engine rbe;
            std::vector<uint8_t> rnd(MASK_SIZE_BYTES);
            std::generate(begin(rnd), end(rnd), std::ref(rbe));

            for(auto i=0; i<MASK_SIZE_BYTES; ++i){
                output_[header_size+i] = rnd[i];
            }

            // update header size
            header_size += MASK_SIZE_BYTES;

            // apply masking information
            LOG_LEVEL(3, "applying mask on output message: 0x%02X%02X%02X%02X", rnd[0], rnd[1], rnd[2], rnd[3]);
            for(auto i=0; i<size; ++i){
                buffer[i] ^= rnd[i%MASK_SIZE_BYTES];
            }
        }

        LOG_LEVEL(3, "sending websocket data. header: %u, payload: %zu, total: %zu", header_size, size, header_size+size);
        // write header
        socket_->write(output_, header_size, ec);
        // write actual payload
        return ec ? 0 : socket_->write(buffer, size, ec);
    }

    void websocket::cancel() {
        if(socket_) socket_->cancel();
    }

    bool websocket::is_binary() const{
        return current_opcode_==0x2;
    }

    void websocket::set_binary(bool binary){
        binary_ = binary;
    }

    void websocket::async_wait(boost::asio::socket_base::wait_type type, ec_handler handler){
        socket_->async_wait(type, std::move(handler));
    }

    size_t websocket::available() const{
        return socket_ ? socket_->available() : 0;
    }
}