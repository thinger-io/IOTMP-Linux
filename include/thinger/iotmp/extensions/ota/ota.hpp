#ifndef THINGER_IOTMP_OTA_HPP
#define THINGER_IOTMP_OTA_HPP

#include "../../client.hpp"

namespace thinger::iotmp{

    class ota{

    public:
        explicit ota(client& client);
        ~ota();

    protected:
        iotmp_resource& resource_;
    };

}


#endif