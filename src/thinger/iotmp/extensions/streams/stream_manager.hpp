#ifndef THINGER_CLIENT_STREAM_MANAGER_HPP
#define THINGER_CLIENT_STREAM_MANAGER_HPP

#include "../../client.hpp"
#include "../../core/iotmp_resource.hpp"
#include "stream_session.hpp"
#include <unordered_map>

namespace thinger::iotmp{

    class stream_manager {

    public:

        stream_manager(client& client, const char* resource) :
            client_(client),
            resource_(client[resource])
        {

            // initialize resource to receive input data
            resource_ = [&](input& in){
                // stop if not bytes received
                if(!in->is_bytes()){
                    return;
                }

                // get buffer size
                size_t size;
                uint8_t* buffer = nullptr;
                in->get_bytes(buffer, size);
                if(!size) return;

                // write input to terminal
                auto stream_id = in.get_stream_id();
                auto session = get_session(stream_id);
                if(session){
                    session->write(buffer, size);
                }else{
                    THINGER_LOG_ERROR("stream id does not correspond with a proxy session: %u", stream_id);
                    stop(stream_id, [](const exec_result& result){});
                }
            };

            // stop resource stream echo
            resource_.set_stream_echo(false);

            // set streaming listener (to detect a stream is open, and it is required to start the stream)
            resource_.set_stream_handler([&](uint16_t stream_id, pson& path_parameters, pson& parameters, bool enabled, result_handler handler){
                if(enabled){
                    //THINGER_LOG("[%u] calling start", stream_id);
                    start(stream_id, path_parameters, parameters, std::move(handler));
                }else{
                    stop(stream_id, handler);
                }
            });
        }

        virtual ~stream_manager() = default;

    protected:

        virtual std::shared_ptr<stream_session> create_session(client& client, uint16_t stream_id, std::string session, pson& parameters) = 0;

        void start(uint16_t stream_id, pson& path_parameters, pson& parameters, result_handler handler){
            auto it = sessions_.find(stream_id);

            // ensure the session is not already available
            if(it!=sessions_.end()) return handler(false);

            // create the session
            auto session = create_session(client_, stream_id, path_parameters["session"], parameters);

            // ensure session is created
            if(session==nullptr) return handler(false);

            // start the session
            session->start([this, stream_id, session, handler=std::move(handler)](const exec_result& result){
                if(result){
                    // store session (as weak ptr)
                    sessions_.emplace(stream_id, session);

                    // set session listener to clean-up once done
                    session->set_on_end_listener([this, stream_id](){
                        stop(stream_id, [](const exec_result& result){

                        });
                    });
                }
                handler(result);
            });
        }

        void stop(uint16_t stream_id, result_handler handler){
            auto it = sessions_.find(stream_id);
            if(it==sessions_.end()) return handler(false);
            auto session = it->second.lock();
            sessions_.erase(it);
            if(session){
                session->stop();
            }else{
                client_.stop_stream(stream_id);
            }
            return handler(true);
        }

        std::shared_ptr<stream_session> get_session(uint16_t stream_id){
            auto it = sessions_.find(stream_id);
            if(it==sessions_.end()) return nullptr;
            return it->second.lock();
        }

        std::shared_ptr<stream_session> get_session(const std::string& session){
            for(const auto& item : sessions_){
                auto it = item.second.lock();
                if(it && it->get_session()==session){
                    return it;
                }
            }
            return nullptr;
        }

    protected:
        // reference to asio client
        client& client_;

        // reference to $console resource
        iotmp_resource& resource_;

        // references to active sessions
        std::unordered_map<uint16_t, std::weak_ptr<stream_session>> sessions_;
    };
}

#endif