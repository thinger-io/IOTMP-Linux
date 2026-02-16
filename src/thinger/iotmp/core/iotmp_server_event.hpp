#ifndef IOTMP_RESOURCE_EVENT_HPP
#define IOTMP_RESOURCE_EVENT_HPP

#include "iotmp_resource.hpp"

namespace thinger{

    class iotmp_server_event : public thinger::iotmp::iotmp_resource{
    public:

        iotmp_server_event(){
            set_stream_echo(false);
        }

        iotmp::json_t& get_params(){
            return event_config_;
        }

        void set_resource(iotmp::server::run resource){
            resource_ = resource;
        }

        iotmp::server::run get_resource() const{
            return resource_;
        }

    private:
        iotmp::server::run resource_ = iotmp::server::run::SUBSCRIBE_EVENT;
        iotmp::json_t event_config_;

    };

}


#endif

