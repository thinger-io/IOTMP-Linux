#ifndef THINGER_RESULT_HPP
#define THINGER_RESULT_HPP

#include "pson.h"
#include <functional>

namespace thinger::iotmp{

    class exec_result{
    public:
        exec_result() : success_(false){

        }

        exec_result(bool success = false) :
            success_(success)
        {

        }

        explicit operator bool() const{
            return success_;
        }

    private:
        bool success_;
        protoson::pson data_;
    };

    typedef std::function<void(const exec_result&)> result_handler;

}

#endif