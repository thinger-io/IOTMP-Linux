#include "thinger_streams.hpp"
#include "thinger.h"

namespace thinger_client{

    static pson empty;

    thinger_streams::thinger_streams(thinger &thinger) : thinger_(thinger){

    }

    void thinger_streams::handle_streams(unsigned long timestamp){
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

    void thinger_streams::start(uint16_t stream_id, thinger_client::thinger_resource &resource, pson& path_params, pson& params) {
        // initialize stream configuration
        auto& stream = stream_resources_[stream_id];
        stream.resource = &resource;
        // TODO check if interval is available instead of creating it automatically here if not present
        stream.interval = params["interval"];
        // if not interval, set stream identifier
        if(stream.interval==0) resource.set_stream_id(stream_id);
        // notify resource listener
        resource.notify_listener(stream_id, path_params, params, true);
    }

    void thinger_streams::stop(uint16_t stream_id, thinger_client::thinger_resource &resource, pson& params) {
        // disable stream on resource
        if(resource.get_stream_id()==stream_id) resource.set_stream_id(0);
        // notify resource listener
        resource.notify_listener(stream_id, params, empty, false);
        // clear entry
        stream_resources_.erase(stream_id);
    }

    thinger_resource *thinger_client::thinger_streams::find(uint16_t stream_id) {
        stream* stream = stream_resources_.find(stream_id);
        return stream!=nullptr ? stream->resource : nullptr;
    }

    void thinger_streams::clear() {
        pson params;
        for(auto it = stream_resources_.begin(); it.valid(); it.next()){
            it.item().right.resource->notify_listener(it.item().left, empty, params, false);
        }
        // clear relation
        stream_resources_.clear();
    }

}

