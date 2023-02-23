#ifndef THINGER_IOTMP_TERMINAL_SESSION_HPP
#define THINGER_IOTMP_TERMINAL_SESSION_HPP

#include "../../client.hpp"
#include "../streams/stream_session.hpp"

namespace thinger::iotmp{

    class terminal_session : public stream_session{

    public:

        terminal_session(client& client, uint16_t stream_id, std::string session, pson& parameters);
        ~terminal_session() override;

        void start(result_handler handler) override;
        bool stop() override;
        void update_params(input& in, output& out);

    private:

        void write(uint8_t* buffer, size_t size) override;
        void handle_read();

        int pid_ = 0;
        unsigned cols_ = 80;
        unsigned rows_ = 60;

        /// terminal command to be used
        std::string terminal_;

        /// stream descriptor for read/write to the term
        boost::asio::posix::stream_descriptor descriptor_;

        std::vector<uint8_t> read_buffer_;

    };

}

#endif