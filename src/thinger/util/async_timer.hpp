#ifndef THINGER_CLIENT_ASYNC_TIMER_HPP
#define THINGER_CLIENT_ASYNC_TIMER_HPP

#include <boost/asio.hpp>

namespace thinger::util{

    class async_timer{

    public:

        async_timer(boost::asio::io_service& io_service) : timer_(io_service){

        }

        virtual ~async_timer() = default;

        void set_timeout(std::chrono::seconds seconds){
            timeout_ = seconds;
        }

        void set_callback(std::function<void()> callback){
            timeout_callback_ = std::move(callback);
        }

        void start(){
            schedule_timeout();
        }

        void stop(){
            timer_.cancel();
            timeout_callback_ = nullptr;
        }

    private:
        void schedule_timeout(){
            if(timeout_.count()<=0) return;
            timer_.expires_from_now(boost::posix_time::seconds(timeout_.count()));
            timer_.async_wait([this](const boost::system::error_code& e){
                if(e != boost::asio::error::operation_aborted){
                    if(timeout_callback_) timeout_callback_();
                    schedule_timeout();
                }
            });
        }

        boost::asio::deadline_timer timer_;
        std::function<void()> timeout_callback_;
        std::chrono::seconds timeout_;
    };
}

#endif