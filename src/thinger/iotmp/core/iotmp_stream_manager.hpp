#ifndef THINGER_IOTMP_STREAM_MANAGER_HPP
#define THINGER_IOTMP_STREAM_MANAGER_HPP

#include "iotmp_resource.hpp"
#include "iotmp_stream_session.hpp"
#include <unordered_map>

// Forward declaration
namespace thinger::iotmp {
    class client;
}

namespace thinger::iotmp {

class stream_manager {

public:
    stream_manager(client& client, const char* resource);

    virtual ~stream_manager() = default;

protected:
    virtual std::shared_ptr<stream_session> create_session(
        client& client, uint16_t stream_id, std::string session, json_t& parameters) = 0;

    void start(uint16_t stream_id, json_t& path_parameters, json_t& parameters, result_handler handler);
    void stop(uint16_t stream_id, result_handler handler);

    std::shared_ptr<stream_session> get_session(uint16_t stream_id);
    std::shared_ptr<stream_session> get_session(const std::string& session);

protected:
    client& client_;
    iotmp_resource& resource_;
    std::unordered_map<uint16_t, std::weak_ptr<stream_session>> sessions_;
};

}

#endif
