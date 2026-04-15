#include "cmd_stream.hpp"
#include "cmd_stream_session.hpp"

namespace thinger::iotmp {

    cmd_stream::cmd_stream(client& client)
        : stream_manager(client, "$cmd/:session")
    {
    }

    std::shared_ptr<stream_session> cmd_stream::create_session(client& client, uint16_t stream_id,
                                                               std::string session, json_t& parameters) {
        return std::make_shared<cmd_stream_session>(client, stream_id, std::move(session), parameters);
    }

}
