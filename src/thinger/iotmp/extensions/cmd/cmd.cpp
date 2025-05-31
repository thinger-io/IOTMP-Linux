#include "cmd.hpp"
#include <boost/asio/io_context.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/process/v2/process.hpp>
#include <boost/process/v2/stdio.hpp>

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
            boost::asio::io_context ioc;
            namespace bp2 = boost::process::v2;

            // readable pipes for stdout and stderr
            boost::asio::readable_pipe stdout_pipe(ioc);
            boost::asio::readable_pipe stderr_pipe(ioc);

            // create the process with the command
            bp2::process proc(
                ioc.get_executor(),
                "/bin/sh",
                {"-c", command},
                bp2::process_stdio{
                    nullptr,        // stdin como null
                    stdout_pipe,    // stdout
                    stderr_pipe     // stderr
                }
            );

            // buffer for stdout
            std::string stdout_data;
            boost::asio::dynamic_string_buffer stdout_buffer(stdout_data, 1024);

            std::function<void(boost::system::error_code, std::size_t)> on_stdout;
            on_stdout = [&](boost::system::error_code ec, std::size_t size) {
                if (!ec) {
                    pout.append(stdout_data.data(), size);
                    stdout_data.erase(0, size);

                    // Continuar leyendo
                    boost::asio::async_read(stdout_pipe, stdout_buffer,
                        boost::asio::transfer_at_least(1), on_stdout);
                }
            };

            // buffer for stderr
            std::string stderr_data;
            boost::asio::dynamic_string_buffer stderr_buffer(stderr_data, 1024);

            std::function<void(boost::system::error_code, std::size_t)> on_stderr;
            on_stderr = [&](boost::system::error_code ec, std::size_t size) {
                if (!ec) {
                    perr.append(stderr_data.data(), size);
                    LOG_ERROR("%.*s", (int)size, stderr_data.data());
                    stderr_data.erase(0, size);

                    // Continuar leyendo
                    boost::asio::async_read(stderr_pipe, stderr_buffer,
                        boost::asio::transfer_at_least(1), on_stderr);
                }
            };

            // init the async read operations on stdout and stderr
            boost::asio::async_read(stdout_pipe, stdout_buffer,
                boost::asio::transfer_at_least(1), on_stdout);
            boost::asio::async_read(stderr_pipe, stderr_buffer,
                boost::asio::transfer_at_least(1), on_stderr);

            // init exit code
            int exit_code = -1;

            // wait until the process finishes
            proc.async_wait([&](boost::system::error_code ec, int native_exit_code) {
                if (!ec) {
                    // keep the exit code
                    exit_code = bp2::evaluate_exit_code(native_exit_code);
                }
                // stop the io_context
                ioc.stop();
            });

            // run the io_context to process the async operations
            ioc.run();

            return exit_code;

        }catch(const std::exception & e) {
            LOG_ERROR("error while executing command: %s", e.what());
            return EXIT_FAILURE;
        }catch(...){
            return EXIT_FAILURE;
        }
    }

}