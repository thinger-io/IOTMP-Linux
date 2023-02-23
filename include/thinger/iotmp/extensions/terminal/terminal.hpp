#ifndef THINGER_IOTMP_TERMINAL_HPP
#define THINGER_IOTMP_TERMINAL_HPP

#include "../streams/stream_manager.hpp"

namespace thinger::iotmp{

    class terminal : public stream_manager{

    public:
        explicit terminal(client& client);

        ~terminal() override = default;

    protected:

        std::shared_ptr<stream_session> create_session(client& client, uint16_t stream_id, std::string session, pson& parameters) override;

    };

}



#endif