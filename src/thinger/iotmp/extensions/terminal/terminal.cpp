#include "terminal.hpp"
#include "terminal_session.hpp"

namespace thinger::iotmp{

    terminal::terminal(thinger::iotmp::client& client) : stream_manager(client, "$terminal/:session")
    {
        // initialize path to receive shell parameters updates (mainly over http requests)
        client["$terminal/:session/params"] = [&](input& in, output& out){
            auto session = get_session(in.path("session", ""));
            if(!session){
                out["error"] = (const char*)strerror(errno);
                out.set_return_code(404);
            }else{
                auto shell_session = std::static_pointer_cast<terminal_session>(session);
                shell_session->update_params(in, out);
            }
        };
    }

    std::shared_ptr<stream_session> terminal::create_session(thinger::iotmp::client& client, uint16_t stream_id, std::string session,
                                                             protoson::pson& parameters){
        return std::make_shared<terminal_session>(client, stream_id, session, parameters);
    }

}
