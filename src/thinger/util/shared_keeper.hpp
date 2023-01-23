#ifndef THINGER_UTIL_SHARED_KEEPER_HPP
#define THINGER_UTIL_SHARED_KEEPER_HPP

#include <boost/asio.hpp>
#include <memory>
#include <utility>
#include "logger.hpp"

namespace thinger::util{

    template<class T>
    class shared_keeper : public std::enable_shared_from_this<shared_keeper<T>>{
    public:

        shared_keeper(boost::asio::io_service& io_service) :
            timer_(io_service)
        {

        }

        virtual ~shared_keeper(){
            timer_.cancel();
        }

        void keep(std::shared_ptr<T> keep_alive_instance, std::function<void()> timeout_handler, std::chrono::seconds seconds = std::chrono::seconds{10}){
            clear();
            shared_instance_ = keep_alive_instance;
            timeout_handler_ = std::move(timeout_handler);
            timeout_seconds_ = seconds;
            check_timeout();
        }

        void clear(){
            timer_.cancel();
            shared_instance_.reset();
        }

        void heartbeat(){
            idle_ = false;
        }

        [[nodiscard]] std::chrono::seconds timeout() const{
            return timeout_seconds_;
        }

        bool timed_out() const{
            return timed_out_;
        }

        void update_interval(std::chrono::seconds interval){
            timeout_seconds_ = interval;
            // cancel current timer
            timer_.cancel();
            // re-schedule timeout
            check_timeout();
        }

    private:

        void check_timeout()
        {
            idle_ = true;
            timed_out_ = false;
            timer_.expires_from_now(boost::posix_time::seconds(timeout_seconds_.count()));
            timer_.async_wait(
                [this, self = std::enable_shared_from_this<shared_keeper<T>>::shared_from_this()](const boost::system::error_code& e){

                    // if timer was not cancelled (just expired by itself)
                    if(e != boost::asio::error::operation_aborted){
                        if(!idle_){
                            check_timeout();
                        }else{
                            timed_out_ = true;
                            if(shared_instance_){
                                timeout_handler_();
                                shared_instance_.reset();
                            }
                        }
                    }else{
                        LOG_LEVEL(3, "shared_keeper cancelled: %s", e.message().c_str());
                        shared_instance_.reset();
                    }
                }
            );
        }

        boost::asio::deadline_timer timer_;
        std::shared_ptr<T> shared_instance_;
        std::function<void()> timeout_handler_;
        std::chrono::seconds timeout_seconds_{0};
        bool idle_ = false;
        bool timed_out_ = false;
    };
}

#endif