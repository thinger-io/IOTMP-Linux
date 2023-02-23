#ifndef THINGER_ASIO_WORKERS
#define THINGER_ASIO_WORKERS

#include <unordered_map>
#include <set>
#include <boost/asio.hpp>
#include "worker_thread.hpp"

namespace thinger::asio {

	class workers {
	public:
        workers();
		virtual ~workers();

		/// start the asio workers
        bool start(size_t working_threads=std::thread::hardware_concurrency());

        /// stop the asio workers and all their pending async operations
        bool stop();

        /// keep the asio workers alive until a signal is received
        void wait(const std::set<unsigned>& signals = {SIGINT, SIGTERM, SIGQUIT});

        /// return an isolated io_service not shared in the pool
        boost::asio::io_service& get_isolated_io_service(std::string thread_name);

        /// return the the next io_service (round robin)
        boost::asio::io_service& get_next_io_service();

        /// return the io_service associated to the caller thread
		boost::asio::io_service& get_thread_io_service();

	private:
        /// mutex used for initializing threads and data structures
        std::mutex mutex_;

        // io_service used for capturing signals
        boost::asio::io_service io_service_;
        boost::asio::signal_set signals_;

        /// flag for controlling if it is running
		std::atomic<bool> running_  = false;

        /// index to control next io_service to use (in the round robin)
        unsigned next_io_service_   = 0;

		/// worker threads used for general asio pool
		std::vector<std::unique_ptr<worker_thread>> worker_threads_;

        /// worker threads allocated from isolated_io_services
        std::vector<std::unique_ptr<worker_thread>> job_threads_;

		/// relation of all worker threads with their worker thread instance
        std::unordered_map<std::thread::id, std::reference_wrapper<worker_thread>> workers_threads_map_;

	};

	extern class workers workers;

}

#endif
