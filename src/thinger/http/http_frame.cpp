#include "../util/logger.hpp"
#include "http_frame.hpp"

namespace thinger::http {

    void http_frame::set_last_frame(bool last_frame) {
        last_frame_ = last_frame;
    }

    bool http_frame::end_stream() {
        return last_frame_;
    }

    void http_frame::log(const char* scope, int level) const{
        LOG_WARNING("[%s] unimplemented log method", scope);
    }

}