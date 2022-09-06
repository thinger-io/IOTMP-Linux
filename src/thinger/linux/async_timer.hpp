#ifndef THINGER_CLIENT_ASYNC_TIMER_HPP
#define THINGER_CLIENT_ASYNC_TIMER_HPP

#include <boost/asio.hpp>

namespace thinger_client{

    class async_timer{

    public:

        async_timer(boost::asio::io_service& io_service) : timer_(io_service){

        }

        virtual ~async_timer(){
            stop();
        }

        void set_timeout(unsigned long seconds){
            this->timeout_seconds_ = seconds;
        }

        void set_callback(std::function<void()> callback){
            this->timeout_callback_ = callback;
        }

        void start(){
            schedule_timeout();
        }

        void stop(){
            timer_.cancel();
        }

    private:
        void schedule_timeout(){
            timer_.expires_from_now(boost::posix_time::seconds(timeout_seconds_));
            timer_.async_wait([this](const boost::system::error_code& e){
                if(e != boost::asio::error::operation_aborted){
                    if(timeout_callback_){
                        timeout_callback_();
                    }
                    schedule_timeout();
                }
            });
        }

    private:
        boost::asio::deadline_timer timer_;
        unsigned long timeout_seconds_;
        std::function<void()> timeout_callback_;
    };
}

#endif