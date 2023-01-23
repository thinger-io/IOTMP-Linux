#include "workers.hpp"
#include <boost/bind/bind.hpp>
#include "../util/logger.hpp"

namespace thinger::asio{

	class workers workers;

	using std::thread;
	using std::bind;
	using std::shared_ptr;
	using std::make_shared;

    workers::workers() :
        signals_(io_service_),
        worker_threads_()
    {

    }

    workers::~workers()
    {

    }

    void workers::wait(const std::set<unsigned>& signals) {
        LOG_LEVEL(1, "registering stop signals...");
        for (auto signal: signals){
            signals_.add(signal);
        }

        // wait for any signal to be received
        signals_.async_wait([this](const boost::system::error_code& ec, int signal_number){
            if(!ec){
                LOG_INFO("received signal: %d", signal_number);
                stop();
            }
        });

        io_service_.run();
    }


    bool workers::start(size_t worker_threads)
    {
        std::scoped_lock<std::mutex> lock(mutex_);
        if(running_) return false;
        running_ = true;

        LOG_INFO("starting %zu working threads in the shared pool", worker_threads);
        worker_threads_.reserve(worker_threads);
        for(auto thread_number=1; thread_number<=worker_threads; ++thread_number){
            auto worker = std::make_unique<worker_thread>("worker thread " + std::to_string(thread_number));
            auto id = worker->start();
            workers_threads_map_.emplace(id, *worker);
            worker_threads_.emplace_back(std::move(worker));
        }

        return running_;
    }

    boost::asio::io_service& workers::get_isolated_io_service(std::string thread_name)
    {
        LOG_INFO("starting '%s' worker thread", thread_name.c_str());
        auto worker = std::make_unique<worker_thread>(std::move(thread_name));
        auto& io_service = worker->get_io_service();
        auto thread_id = worker->start();
        auto& worker_ref = *worker;
        job_threads_.emplace_back(std::move(worker));
        workers_threads_map_.emplace(thread_id, worker_ref);
        return io_service;
    }

    bool workers::stop()
    {
        std::scoped_lock<std::mutex> lock(mutex_);
        if(!running_) return false;
        running_ = false;

        LOG_INFO("stopping job threads");
        for(auto const& worker_thread : job_threads_){
            worker_thread->stop();
        }

        LOG_INFO("stopping worker threads");
        for(auto const& worker_thread : worker_threads_){
            worker_thread->stop();
        }

        // clear auxiliary references
        LOG_INFO("clearing structures");
        //workers_threads_map_.clear();
        //worker_threads_.clear();
        //job_threads_.clear();

        // stop io_service used in signals
        LOG_INFO("stopping io_service");
        io_service_.stop();

        LOG_INFO("all done!");

        return !running_;
    }

	boost::asio::io_service& workers::get_next_io_service()
	{
        return worker_threads_[next_io_service_++%worker_threads_.size()]->get_io_service();
	}

	boost::asio::io_service& workers::get_thread_io_service()
	{
        std::scoped_lock<std::mutex> lock(mutex_);
        std::thread::id this_id = std::this_thread::get_id();
        auto it = workers_threads_map_.find(this_id);
        if(it==workers_threads_map_.end()){
            //LOG_F(ERROR, "trying to get the thread io service outside a worker thread");
            return worker_threads_.begin()->get()->get_io_service();
        }else{
            return it->second.get().get_io_service();
        }
	}

    /*
    char logger alignas(workers)[sizeof(workers)];
    workers * plogger = nullptr;

    struct init{
        init(){
            plogger=new(&logger)workers{};
        }
        ~init(){
            plogger->~workers();
            plogger=nullptr;
        }
    };
     */

}