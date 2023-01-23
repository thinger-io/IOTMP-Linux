#include "version.hpp"

namespace thinger::iotmp{

    version::version(thinger::iotmp::client& client){

        client["version"] = [](output& out){
            /*
            out["major"] = VERSION_MAJOR;
            out["minor"] = VERSION_MINOR;
            out["patch"] = VERSION_PATCH;
            out["version"] = std::to_string(VERSION_MAJOR) + "." + std::to_string(VERSION_MINOR) + "." + std::to_string(VERSION_PATCH);
             */
        };

    }

}