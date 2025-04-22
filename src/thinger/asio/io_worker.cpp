#include "io_worker.hpp"
#include "../util/logger.hpp"

namespace thinger::asio{

    io_worker::io_worker() :
        io_{1},
        work_(boost::asio::make_work_guard(io_)){
        LOG_INFO("io worker created");
    }

    io_worker::~io_worker() {
        LOG_TRACE("io worker released");
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

    boost::asio::io_context& io_worker::get_io_context(){
        return io_;
    }

}