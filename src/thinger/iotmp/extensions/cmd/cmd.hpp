#ifndef THINGER_IOTMP_CMD_HPP
#define THINGER_IOTMP_CMD_HPP

#include "../../client.hpp"
#include <vector>

namespace thinger::iotmp{

    class cmd{

    public:
        explicit cmd(client& client);
        virtual ~cmd() = default;

        /**
         * Execute a program directly with arguments, optional stdin, and optional timeout.
         * @param executable  Path to the executable
         * @param args        Arguments to pass to the executable
         * @param out         Captured stdout
         * @param err         Captured stderr
         * @param stdin_data  Data to write to the process stdin (empty = no stdin)
         * @param timeout_seconds  Timeout in seconds (0 = no timeout)
         * @return exit code of the process, or -1 on failure
         */
        static int exec(const std::string& executable,
                        const std::vector<std::string>& args,
                        std::string& out,
                        std::string& err,
                        const std::string& stdin_data = "",
                        int timeout_seconds = 0,
                        bool* timed_out = nullptr);

    };

}


#endif
