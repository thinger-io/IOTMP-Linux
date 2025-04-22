#ifndef THINGER_ASIO_WORKER_THREAD_HPP
#define THINGER_ASIO_WORKER_THREAD_HPP

#include <thread>
#include "io_worker.hpp"

namespace thinger::asio{
    class worker_thread {
    public:
        worker_thread(std::string worker_name = "");
        virtual ~worker_thread();

        void set_thread_name(std::string worker_name);

        boost::asio::io_context& get_io_context();

        std::thread::id start();
        bool stop();
        void run(std::function<void()> handler);

    protected:
        virtual void async_worker(){};

    private:
        std::thread thread_;
        std::string worker_name_;
        io_worker worker_;
    };
}


#endif