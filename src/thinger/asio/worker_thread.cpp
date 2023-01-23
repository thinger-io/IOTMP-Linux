#include "worker_thread.hpp"
#include "../util/logger.hpp"
#include <future>

namespace thinger::asio{

    std::thread::id worker_thread::start() {
        if(thread_.joinable()) return thread_.get_id();

        std::promise<std::thread::id> promise;
        thread_ = std::thread([this, &promise]{
            if(!worker_name_.empty()){
#ifdef THINGER_LOG_LOGURU
                loguru::set_thread_name(worker_name_.c_str());
#endif
            }
            LOG_LEVEL(1, "worker thread started");

            // start the async work in the child
            async_worker();

            // flag start to true, so the caller can return
            promise.set_value(thread_.get_id());

            // call async worker
            worker_.start();

            // stop
            LOG_LEVEL(1, "worker thread stopped");
        });

        // wait for thread to be initialized
        auto future = promise.get_future();
        future.wait();
        return future.get();
    }

    bool worker_thread::stop() {
        if(!thread_.joinable()) return false;
        LOG_INFO("stopping worker thread: %s", worker_name_.c_str());
        worker_.stop();
        thread_.join();
        return true;
    }

    boost::asio::io_service &worker_thread::get_io_service() {
        return worker_.get_io_service();
    }

    worker_thread::worker_thread(std::string worker_name) :
        worker_name_(std::move(worker_name))
    {
        LOG_LEVEL(2, "worker thread created: %s", worker_name_.c_str());
    }

    worker_thread::~worker_thread(){
        LOG_LEVEL(2, "worker thread deleted: %s", worker_name_.c_str());
    }

    void worker_thread::set_thread_name(std::string worker_name){
        worker_name_ = std::move(worker_name);
    }

    void worker_thread::run(std::function<void()> handler) {
        worker_.get_io_service().dispatch(std::move(handler));
    }

}