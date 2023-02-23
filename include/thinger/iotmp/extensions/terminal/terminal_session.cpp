#include "terminal_session.hpp"

#include <boost/process.hpp>
#include <boost/asio.hpp>
#include <boost/circular_buffer.hpp>
#include <filesystem>
#include "../../../util/logger.hpp"

#ifdef __linux__
#include <pty.h>
#elif __APPLE__
#include <util.h>
#endif

#define BUFFER_SIZE 1024

namespace thinger::iotmp{

    terminal_session::terminal_session(client& client, uint16_t stream_id, std::string session,
                                       pson& parameters)
            : stream_session(client, stream_id, std::move(session)),
              descriptor_(client.get_io_service()),
              read_buffer_(BUFFER_SIZE)
    {
        // check terminal to use
        std::vector<std::string> terminals{"zsh", "bash", "sh", "ash"};
        for(auto& terminal : terminals){
            if(std::filesystem::exists("/bin/" + terminal)){
                terminal_ = terminal;
                break;
            }
        }

        // initialize cols and rows from parameters
        cols_ = parameters["cols"];
        rows_ = parameters["rows"];

        std::stringstream result;
        json_encoder encoder(result);
        encoder.encode(parameters);
        THINGER_LOG("received shell parameters: %s", result.str().c_str());
    }

    terminal_session::~terminal_session(){
        THINGER_LOG("removing terminal: '%s' with %d (cols) %d (rows)", terminal_.c_str(), cols_, rows_);
    }

    void terminal_session::write(uint8_t* buffer, size_t size){
        descriptor_.async_write_some(boost::asio::const_buffer(buffer, size), [this, self = shared_from_this()](const boost::system::error_code& ec, std::size_t bytes_transferred){
            if(ec) stop();
        });
    }

    void terminal_session::handle_read(){
        descriptor_.async_read_some(boost::asio::buffer(read_buffer_, BUFFER_SIZE), [this, self = shared_from_this()](const boost::system::error_code &ec, std::size_t bytes_transferred){
            if(ec){
                if(ec != boost::asio::error::operation_aborted){
                    THINGER_LOG_ERROR("error while reading from terminal: %d (%s)", ec.value(), ec.message().c_str());
                }
                stop();
                return;
            }
            if(bytes_transferred){
                try{
                    client_.stream_resource(stream_id_, &read_buffer_[0], bytes_transferred);
                }catch(const std::exception & e) {
                    THINGER_LOG_ERROR("cannot read from pty: %s", e.what());
                }catch(...){
                    THINGER_LOG_ERROR("unknown error while reading from pty");
                }
            }
            handle_read();
        });
    }

    void terminal_session::start(result_handler handler){
        if(pid_ || descriptor_.is_open() || terminal_.empty()) return handler(false);
        THINGER_LOG("initializing terminal '%s' with %d (cols) %d (rows)", terminal_.c_str(), cols_, rows_);

        int master;
        char name[100];

        winsize winsize;
        winsize.ws_col = cols_;
        winsize.ws_row = rows_;

        std::string term_path = "/bin/" + terminal_;
        THINGER_LOG("starting terminal: %s (%s) -> %d (cols) %d (rows)", term_path.c_str(), terminal_.c_str(), winsize.ws_col, winsize.ws_row);

        pid_ = forkpty(&master, name, nullptr, &winsize);

        if(pid_==0){ // child process
            // set color terminal
            setenv ("TERM", "xterm-256color", 1);

            // execute terminal
            exit(execlp(term_path.c_str(), terminal_.c_str(), "-l", "-i", "-s", nullptr));
        }else if(pid_>0){
            THINGER_LOG("terminal started (pid %d): %s", pid_, name);
            // parent process just open the descriptor
            descriptor_ = boost::asio::posix::stream_descriptor(client_.get_io_service(), master);
            handle_read();
            return handler(true);
        }

        return handler(false);
    }

    bool terminal_session::stop(){
        auto ret_val = true;

        // stop terminal process
        if(pid_){
            int result = kill(pid_, SIGHUP);
            THINGER_LOG("stopping terminal (%d): %s", pid_, result==0 ? "ok" : "error");
            pid_ = 0;
            ret_val &= result==0;
        }

        // close stream
        if(descriptor_.is_open()){
            boost::system::error_code ec;
            descriptor_.close(ec);
            ret_val &= !((bool)ec);
        }

        return ret_val;
    }

    void terminal_session::update_params(input& in, output& out){
        // get input cols & rows
        unsigned cols = in["size"]["cols"];
        unsigned rows = in["size"]["rows"];

        // check if the parms are ok
        if(cols==0 || rows==0){
            out["error"] = "rows and cols must be greater than 0";
            out.set_return_code(400);
        }else if(descriptor_.is_open()){
            winsize winsize;
            winsize.ws_col = cols;
            winsize.ws_row = rows;
            if(ioctl(descriptor_.native_handle(), TIOCSWINSZ, &winsize)!=0){
                out["error"] = (const char*)strerror(errno);
                out.set_return_code(400);
            }else{
                cols_ = cols;
                rows_ = rows;
                THINGER_LOG("updated terminal size to %ux%u (cols, rows)", cols_, rows_);
            }
        }else{
            out["error"] = "terminal is not active";
            out.set_return_code(404);
        }
    }
}