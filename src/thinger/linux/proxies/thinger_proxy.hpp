#ifndef THINGER_CLIENT_PROXY_HPP
#define THINGER_CLIENT_PROXY_HPP

#include "../streams/stream_manager.hpp"
#include "tcp_proxy_session.hpp"

namespace thinger_client{

    class thinger_proxy : public stream_manager{

    public:
        explicit thinger_proxy(asio_client& client) :
            stream_manager(client, "$proxy/:session")
        {

        }

        ~thinger_proxy() override = default;

    protected:
        std::shared_ptr<stream_session> create_session(asio_client& client, uint16_t stream_id, std::string session, pson& parameters) override {
            std::string protocol = parameters["protocol"];
            std::string host     = parameters["address"];
            uint16_t    port     = parameters["port"];

            if(protocol=="tcp"){
                return std::make_shared<tcp_proxy_session>(client, stream_id, session, host, port);
            }

            return nullptr;
        }

    };

}


#endif