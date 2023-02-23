#include "proxy.hpp"
#include "proxy_session.hpp"

namespace thinger::iotmp{

    proxy::proxy(client& client) :
            stream_manager(client, "$proxy/:session")
    {

    }

    std::shared_ptr<stream_session>
    proxy::create_session(client& client, uint16_t stream_id, std::string session, pson& parameters){
        //THINGER_LOG("[%u] create session called", stream_id);
        std::string protocol = parameters["protocol"];
        std::string host     = parameters["address"];
        uint16_t    port     = parameters["port"];

        if(protocol=="tcp"){
            return std::make_shared<proxy_session>(client, stream_id, session, host, port);
        }

        return nullptr;
    }
}