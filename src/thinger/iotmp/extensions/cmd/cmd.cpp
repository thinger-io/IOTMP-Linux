#include "cmd.hpp"
#include <boost/asio/io_service.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/process.hpp>

namespace thinger::iotmp{

    cmd::cmd(thinger::iotmp::client& client)
    {
        // initialize cmd resource
        client["cmd"] = [this](input& in, output& out){
            if(in.describe()){
                in["command"]   = "";
                in["mode"]      = "api";
                out["retcode"]  = 0;
                out["stdout"]   = "";
                out["stderr"]   = "";
            }else{
                std::string pout;
                std::string perr;
                auto retcode = exec(in["command"], pout, perr);

                std::string mode = in["mode"];
                if(mode=="api" || mode == ""){
                    out["retcode"] = retcode;
                    out["stdout"]  = pout;
                    out["stderr"]  = perr;
                }else if(mode=="text"){
                    if(!pout.empty()){
                        out = pout;
                    }else{
                        out = perr;
                    }
                }
            }
        };
    }

    std::vector<std::string> tokenize(const std::string& in) {
        char sep = ' ';
        std::string::size_type b = 0;
        std::vector<std::string> result;

        while ((b = in.find_first_not_of(sep, b)) != std::string::npos) {
            auto e = in.find_first_of(sep, b);
            result.push_back(in.substr(b, e-b));
            b = e;
        }
        return result;
    }

    int cmd::exec(const std::string& command, std::string& pout, std::string& perr)
    {
        LOG_INFO("$ %s", command.c_str());

        try{
            boost::asio::io_service svc;
            namespace bp = boost::process;

            bp::async_pipe std_out(svc);
            bp::async_pipe std_err(svc);

            std::vector<std::string> args = {"-c", command};
            bp::child c(bp::search_path("sh"), bp::args(args), bp::std_out > std_out, bp::std_err > std_err);

            // std cout pipe
            std::vector<char> out;
            out.resize(1024);
            std::function<void(const boost::system::error_code & ec, std::size_t n)> out_read = [&](const boost::system::error_code &ec, std::size_t size){
                if(size){
                    try{
                        pout = {&out[0], size};
                    }catch(const std::exception & e) {
                        LOG_ERROR("error while calling result callback: %s", e.what());
                    }catch(...){
                        LOG_ERROR("error while calling result callback");
                    }
                }
                if(!ec && c.running()){
                    std_out.async_read_some(boost::asio::buffer(out, 1024), out_read);
                }
            };
            std_out.async_read_some(boost::asio::buffer(out, 1024), out_read);

            // std err pipe
            std::vector<char> err;
            err.resize(1024);
            std::function<void(const boost::system::error_code & ec, std::size_t n)> error_read = [&](const boost::system::error_code &ec, std::size_t size){
                if(size){
                    try{
                        perr = {&err[0], size};
                        LOG_ERROR("%s", perr.c_str());
                    }catch(const std::exception & e) {
                        LOG_ERROR("error while calling result callback: %s", e.what());
                    }catch(...){
                        LOG_ERROR("error while calling result callback");
                    }
                }
                if(!ec && c.running()){
                    std_err.async_read_some(boost::asio::buffer(err, 1024), error_read);
                }
            };
            std_err.async_read_some(boost::asio::buffer(err, 1024), error_read);

            svc.run();
            c.wait();
            return c.exit_code();
        }catch(const std::exception & e) {
            LOG_ERROR("error while executing command: %s", e.what());
            return EXIT_FAILURE;
        }catch(...){
            return EXIT_FAILURE;
        }
    }


}
