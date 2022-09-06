#ifndef THINGER_CLIENT_STREAM_MANAGER_HPP
#define THINGER_CLIENT_STREAM_MANAGER_HPP

#include "../asio_client.hpp"
#include "../../core/thinger_resource.hpp"
#include "stream_session.hpp"

namespace thinger_client{

    class stream_manager {

    public:

        stream_manager(asio_client& client, const char* resource) :
            client_(client),
            resource_(client[resource])
        {

            // initialize resource to receive input data
            resource_ = ([&](input& in){
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
                    stop(stream_id);
                }
            });

            // stop resource stream echo
            resource_.set_stream_echo(false);

            // set streaming listener (to detect a stream is open, and it is required to start the stream)
            resource_.set_stream_listener([&](uint16_t stream_id, pson& path_parameters, pson& parameters, bool enabled){
                if(enabled){
                    start(stream_id, path_parameters, parameters);
                }else{
                    stop(stream_id);
                }
            });
        }

        virtual ~stream_manager() = default;

    protected:

        virtual std::shared_ptr<stream_session> create_session(asio_client& client, uint16_t stream_id, std::string session, pson& parameters) = 0;

        bool start(uint16_t stream_id, pson& path_parameters, pson& parameters){
            auto it = sessions_.find(stream_id);

            if(it!=sessions_.end()) return false;

            auto session = create_session(client_, stream_id, path_parameters["session"], parameters);
            if(session){
                // store session weak ptr
                sessions_.emplace(stream_id, session);

                // set session listener to clean-up once done
                session->set_on_end_listener([this, stream_id](){
                    stop(stream_id);
                });

                // start session
                session->start();

                return true;
            }

            return false;
        }

        bool stop(uint16_t stream_id){
            auto it = sessions_.find(stream_id);
            if(it==sessions_.end()) return false;
            auto session = get_session(stream_id);
            if(session){
                session->stop();
            }else{
                client_.stop_stream(stream_id);
            }
            sessions_.erase(it);
            return true;
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
        asio_client& client_;

        // reference to $console resource
        thinger_resource& resource_;

        // references to active sessions
        std::unordered_map<uint16_t, std::weak_ptr<stream_session>> sessions_;
    };
}

#endif