#ifndef THINGER_IOTMP_VERSION_HPP
#define THINGER_IOTMP_VERSION_HPP

#include "../../client.hpp"

namespace thinger::iotmp{

    class version{
    public:
        explicit version(client& client);
        virtual ~version() = default;
    };

}


#endif