#include "iotmp_stream_manager.hpp"
#include "../client.hpp"

namespace thinger::iotmp {

stream_manager::stream_manager(client& client, const char* resource)
    : client_(client),
      resource_(client[resource])
{
    // Initialize resource to receive input data (binary chunks or JSON ACKs)
    resource_ = [this](input& in) {
        auto stream_id = in.get_stream_id();
        auto session = get_session(stream_id);
        if(session) {
            session->handle_input(in);
        } else {
            THINGER_LOG_ERROR("stream id does not correspond with a session: {}", stream_id);
            stop(stream_id, [](const exec_result&) {});
        }
    };

    // Stop resource stream echo
    resource_.set_stream_echo(false);

    // Set streaming listener
    resource_.set_stream_handler(
        [this](uint16_t stream_id, json_t& path_parameters, json_t& parameters,
               bool enabled, result_handler handler) {
            if(enabled) {
                start(stream_id, path_parameters, parameters, std::move(handler));
            } else {
                stop(stream_id, handler);
            }
        });
}

void stream_manager::start(uint16_t stream_id, json_t& path_parameters,
                           json_t& parameters, result_handler handler) {
    auto it = sessions_.find(stream_id);

    // Ensure the session is not already available
    if(it != sessions_.end()) {
        return handler(false);
    }

    // Create the session
    auto session = create_session(client_, stream_id,
        get_value(path_parameters, "session", empty::string), parameters);

    // Ensure session is created
    if(!session) {
        return handler(false);
    }

    // Launch coroutine to start the session
    co_spawn(client_.get_io_context(),
        [this, stream_id, session, handler = std::move(handler)]() mutable -> awaitable<void> {
            // Start session using coroutine
            auto result = co_await session->start();

            if(result) {
                // Store session (as weak ptr)
                sessions_.emplace(stream_id, session);

                // Set session listener to clean-up once done
                session->set_on_end_listener([this, stream_id]() {
                    stop(stream_id, [](exec_result&&) {});
                });
            }

            // Call the handler with the result
            handler(std::move(result));
        },
        detached);
}

void stream_manager::stop(uint16_t stream_id, result_handler handler) {
    auto it = sessions_.find(stream_id);
    if(it == sessions_.end()) {
        return handler(false);
    }

    auto session = it->second.lock();
    sessions_.erase(it);

    if(session) {
        session->stop();
    } else {
        client_.stop_stream(stream_id);
    }

    return handler(true);
}

std::shared_ptr<stream_session> stream_manager::get_session(uint16_t stream_id) {
    auto it = sessions_.find(stream_id);
    if(it == sessions_.end()) return nullptr;
    return it->second.lock();
}

std::shared_ptr<stream_session> stream_manager::get_session(const std::string& session) {
    for(const auto& [id, weak_session] : sessions_) {
        auto ptr = weak_session.lock();
        if(ptr && ptr->get_session() == session) {
            return ptr;
        }
    }
    return nullptr;
}

}
