#ifndef THINGER_CLIENT_STREAMS_HPP
#define THINGER_CLIENT_STREAMS_HPP

#include "iotmp_resource.hpp"
#include "thinger_map.hpp"

namespace thinger::iotmp{

    class iotmp;

    struct stream{
        iotmp_resource* resource     = nullptr;
        unsigned int interval        = 0;
        unsigned long last_streaming = 0;
    };

    class iotmp_stream{

    public:
        explicit iotmp_stream(iotmp& thinger);
        ~iotmp_stream()  = default;

        iotmp_resource* find(uint16_t stream_id);

        void start(uint16_t stream_id, iotmp_resource& resource, pson& path_params, pson& params, result_handler handler={});

        void stop(uint16_t stream_id, iotmp_resource& resource, pson& params, result_handler handler={});

        void clear();

        void handle_streams(unsigned long timestamp);

    private:
        iotmp& thinger_;
        // stream id, resource, stream config
        thinger_map<uint16_t, stream> stream_resources_;
    };
}

#endif