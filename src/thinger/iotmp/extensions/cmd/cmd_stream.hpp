#ifndef THINGER_IOTMP_CMD_STREAM_HPP
#define THINGER_IOTMP_CMD_STREAM_HPP

#include "../../core/iotmp_stream_manager.hpp"
#include <memory>
#include <string>

namespace thinger::iotmp {

    class cmd_stream : public stream_manager {

    public:
        explicit cmd_stream(client& client);
        ~cmd_stream() override = default;

    protected:
        std::shared_ptr<stream_session> create_session(client& client, uint16_t stream_id,
                                                      std::string session, json_t& parameters) override;
    };

}

#endif
