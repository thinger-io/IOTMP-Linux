#ifndef THINGER_CLIENT_SHELL_HPP
#define THINGER_CLIENT_SHELL_HPP

#include "../streams/stream_manager.hpp"
#include "thinger_shell_session.hpp"

namespace thinger_client{

    class thinger_shell : public stream_manager{

    public:
        explicit thinger_shell(asio_client& client) : stream_manager(client, "$console/:session")
        {
            // initialize path to receive shell parameters updates (mainly over http requests)
            client["$console/:session/params"] = [&](input& in, output& out){

                auto session = get_session(in.path("session", ""));
                if(!session){
                    out["error"] = (const char*)strerror(errno);
                    out.set_return_code(404);
                }else{
                    auto shell_session = std::static_pointer_cast<thinger_shell_session>(session);
                    shell_session->update_params(in, out);
                }
            };
        }

        ~thinger_shell() override = default;

    protected:

        std::shared_ptr<stream_session> create_session(asio_client& client, uint16_t stream_id, std::string session, pson& parameters) override {
            return std::make_shared<thinger_shell_session>(client, stream_id, session, parameters);
        }

    };

}



#endif