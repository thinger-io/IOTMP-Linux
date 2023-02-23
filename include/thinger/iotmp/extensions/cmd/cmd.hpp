#ifndef THINGER_IOTMP_CMD_HPP
#define THINGER_IOTMP_CMD_HPP

#include "../../client.hpp"

namespace thinger::iotmp{

    class cmd{

    public:
        explicit cmd(client& client);
        virtual ~cmd() = default;

    protected:

        int exec(const std::string& command, std::string& out, std::string& err);
    };

}


#endif