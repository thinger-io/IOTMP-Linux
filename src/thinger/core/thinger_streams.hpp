#ifndef THINGER_CLIENT_STREAMS_HPP
#define THINGER_CLIENT_STREAMS_HPP

#include "thinger_resource.hpp"
#include "thinger_map.hpp"

namespace thinger_client{

    class thinger;

    struct stream{
        thinger_resource* resource   = nullptr;
        unsigned int interval        = 0;
        unsigned long last_streaming = 0;
    };

    class thinger_streams{

    public:
        explicit thinger_streams(thinger& thinger);
        ~thinger_streams()  = default;

        thinger_resource* find(uint16_t stream_id);

        void start(uint16_t stream_id, thinger_resource& resource, pson& path_params, pson& params);

        void stop(uint16_t stream_id, thinger_resource& resource, pson& params);

        void clear();

        void handle_streams(unsigned long timestamp);

    private:
        thinger& thinger_;
        // stream id, resource, stream config
        thinger_map<uint16_t, stream> stream_resources_;
    };
}

#endif