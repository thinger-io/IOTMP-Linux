#include "proxy.hpp"
#include "proxy_session.hpp"

namespace thinger::iotmp{

    proxy::proxy(client& client) :
            stream_manager(client, "$proxy/:session")
    {

    }

    std::shared_ptr<stream_session>
    proxy::create_session(client& client, uint16_t stream_id, std::string session, json_t& parameters){
        //THINGER_LOG("[%u] create session called", stream_id);
        std::string protocol = get_value(parameters, "protocol", empty::string);
        std::string host     = get_value(parameters, "address", empty::string);
        uint16_t    port     = get_value(parameters, "port", (uint16_t)0);
        bool        secure   = get_value(parameters, "secure", false);

        if(protocol=="tcp"){
            return std::make_shared<proxy_session>(client, stream_id, session, host, port, secure);
        }

        return nullptr;
    }
}