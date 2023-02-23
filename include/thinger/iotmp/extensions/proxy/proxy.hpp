#ifndef THINGER_IOTMP_PROXY_HPP
#define THINGER_IOTMP_PROXY_HPP

#include "../../client.hpp"
#include "../streams/stream_manager.hpp"

namespace thinger::iotmp{

    class proxy : public stream_manager{

    public:
        explicit proxy(client& client);
        ~proxy() override = default;

    protected:
        std::shared_ptr<stream_session> create_session(client& client, uint16_t stream_id, std::string session, pson& parameters) override;

    };

}


#endif