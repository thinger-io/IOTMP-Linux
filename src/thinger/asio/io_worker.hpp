#ifndef THINGER_ASIO_IO_WORKER_HPP
#define THINGER_ASIO_IO_WORKER_HPP

#include <boost/asio/io_service.hpp>

namespace thinger::asio{

    class io_worker {
    public:
        io_worker();
        virtual ~io_worker();

        void start();
        void stop();
        boost::asio::io_service& get_io_service();

    private:
        boost::asio::io_service io_;
        boost::asio::io_service::work work_;
    };

}

#endif