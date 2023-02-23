#include "io_worker.hpp"
#include "../util/logger.hpp"

namespace thinger::asio{

    io_worker::io_worker() : io_{1}, work_{io_}{
        LOG_LEVEL(3, "io worker created");
    }

    io_worker::~io_worker() {
        LOG_LEVEL(3, "io worker released");
    }

    void io_worker::start() {
        // a thread that is running forever with a try catch over the io service run so it can recover from errors
        for (;;) {
            try {
                io_.run();
                break; // run() exited normally
            } catch (const std::exception &ex) {
                LOG_ERROR("thread crashed: %s", ex.what());
            } catch (const std::string &ex) {
                LOG_ERROR("thread crashed: %s", ex.c_str());
            } catch (...) {
                LOG_ERROR("unknown thread crash");
            }
            LOG_WARNING("restarting thread");
            io_.restart();
        }
    }

    void io_worker::stop(){
        io_.stop();
    }

    boost::asio::io_service& io_worker::get_io_service(){
        return io_;
    }

}