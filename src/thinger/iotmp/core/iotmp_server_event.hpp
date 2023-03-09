#ifndef IOTMP_RESOURCE_EVENT_HPP
#define IOTMP_RESOURCE_EVENT_HPP

#include "iotmp_resource.hpp"

namespace thinger{

    class iotmp_server_event : public thinger::iotmp::iotmp_resource{
    public:

        iotmp_server_event(){
            set_stream_echo(false);
        }

        protoson::pson& get_params(){
            return event_config_;
        }

        void set_resource(iotmp::server::run resource){
            this->resource_ = resource;
        }

        iotmp::server::run get_resource() const{
            return this->resource_;
        }

    private:
        iotmp::server::run resource_ = iotmp::server::run::NONE;
        protoson::pson event_config_;

    };

}


#endif

