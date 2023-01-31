#include "iotmp_stream.hpp"
#include "iotmp.h"

namespace thinger::iotmp{

    static pson empty;

    iotmp_stream::iotmp_stream(iotmp &thinger) : thinger_(thinger){

    }

    void iotmp_stream::handle_streams(unsigned long timestamp){
        for(auto it = stream_resources_.begin(); it.valid(); it.next()){
            auto& stream = it.item().right;
            if(stream.interval>0){
                if(timestamp-stream.last_streaming>=stream.interval){
                    stream.last_streaming = timestamp;
                    thinger_.stream_resource(*(stream.resource), it.item().left);
                }
            }
        }
    }

    void iotmp_stream::start(uint16_t stream_id, iotmp_resource &resource, pson& path_params, pson& params, result_handler handler) {
        // initialize stream configuration
        auto& stream = stream_resources_[stream_id];
        stream.resource = &resource;

        // initialize interval if available on params
        if(params.is_object() && ((pson_object&) params).contains("interval")){
            stream.interval = params["interval"];
        }

        // if not interval, set stream identifier
        if(stream.interval==0) resource.set_stream_id(stream_id);

        // custom stream handler?
        if(resource.has_stream_handler()){
            //THINGER_LOG("[%u] calling stream handler", stream_id);
            resource.handle_stream(stream_id, path_params, params, true, std::move(handler));
        }else if(handler){
            handler(true);
        }
    }

    void iotmp_stream::stop(uint16_t stream_id, iotmp_resource &resource, pson& params, result_handler handler) {
        // disable stream on resource
        if(resource.get_stream_id()==stream_id) resource.set_stream_id(0);

        // clear entry
        stream_resources_.erase(stream_id);

        // custom stream handler?
        if(resource.has_stream_handler()){
            // notify resource listener
            return resource.handle_stream(stream_id, params, empty, false, std::move(handler));
        }else{
            handler(true);
        }
    }

    iotmp_resource *iotmp_stream::find(uint16_t stream_id) {
        stream* stream = stream_resources_.find(stream_id);
        return stream!=nullptr ? stream->resource : nullptr;
    }

    void iotmp_stream::clear() {
        pson params;
        for(auto it = stream_resources_.begin(); it.valid(); it.next()){
            it.item().right.resource->set_stream_id(0);
            it.item().right.resource->handle_stream(it.item().left, empty, params, false, [](const exec_result& result){

            });
        }
        // clear relation
        stream_resources_.clear();
    }

}

