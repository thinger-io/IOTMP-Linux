#ifndef THINGER_HTTP_FRAME_HPP
#define THINGER_HTTP_FRAME_HPP

#include "data/out_data.hpp"

namespace thinger::http {

    class http_frame : public data::out_data {
    public:
        // constructors
        http_frame() = default;
        ~http_frame() override = default;

        // stream information
        void set_last_frame(bool last_frame);
        virtual bool end_stream();

        // debug
        virtual void log(const char* scope, int level) const;

    private:
        bool last_frame_ = true;

    };

}

#endif