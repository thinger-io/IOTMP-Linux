#ifndef THINGER_ASIO_IO_WORKER_HPP
#define THINGER_ASIO_IO_WORKER_HPP

#include <boost/asio/io_context.hpp>

namespace thinger::asio{

    class io_worker {
    public:
        io_worker();
        virtual ~io_worker();

        void start();
        void stop();
        boost::asio::io_context& get_io_context();

    private:
        boost::asio::io_context io_;
        boost::asio::executor_work_guard<boost::asio::io_context::executor_type> work_;
    };

}

#endif