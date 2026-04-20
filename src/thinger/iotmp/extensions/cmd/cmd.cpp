#include "cmd.hpp"
#include "../../util/shell.hpp"
#include <boost/asio/io_context.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/readable_pipe.hpp>
#include <boost/asio/writable_pipe.hpp>
#include <boost/process/v2/process.hpp>
#include <boost/process/v2/stdio.hpp>
#include <filesystem>
#include <fstream>
#include <random>

namespace thinger::iotmp{

    cmd::cmd(client& client)
    {
        ensure_home_env();
        // initialize cmd resource
        client["cmd"] = [this](input& in, output& out){
            if(in.describe()){
                in["cmd"]       = "";
                in["mode"]      = "api";
                in["timeout"]   = 30;
                in["stdin"]     = "";
                out["retcode"]  = 0;
                out["stdout"]   = "";
                out["stderr"]   = "";
            }else{
                // Isolate the entire execution from the caller's message loop:
                // Boost.Process v2, pipe setup and shell materialization can
                // throw (e.g. pidfd_open ENOSYS on pre-5.3 kernels surfaces
                // as "assign: Bad file descriptor"), and we must never let
                // those escape and tear down the IOTMP connection cycle.
                try{
                    std::string pout;
                    std::string perr;
                    auto command = get_value(in.payload(), "cmd", empty::string);
                    auto timeout = get_value(in.payload(), "timeout", 30);
                    auto stdin_data = get_value(in.payload(), "stdin", empty::string);
                    bool timeout_flag = false;
                    int retcode = 0;

                    if(command.rfind("#!", 0) == 0){
                        namespace fs = std::filesystem;
                        std::random_device rd;
                        std::uniform_int_distribution<uint64_t> dist;
                        char buf[17];
                        std::snprintf(buf, sizeof(buf), "%016llx", (unsigned long long) dist(rd));
                        auto tmp_path = fs::temp_directory_path() / (std::string("thinr-cmd-") + buf);
                        std::error_code ec;

                        {
                            std::ofstream ofs(tmp_path, std::ios::binary);
                            ofs.write(command.data(), command.size());
                        }
                        fs::permissions(tmp_path,
                            fs::perms::owner_read | fs::perms::owner_write | fs::perms::owner_exec,
                            fs::perm_options::replace, ec);

                        retcode = exec(tmp_path.string(), {}, pout, perr, stdin_data, timeout, &timeout_flag);

                        fs::remove(tmp_path, ec);
                    }else{
                        retcode = exec(preferred_shell(), {"-c", command}, pout, perr, stdin_data, timeout, &timeout_flag);
                    }

                    if(timeout_flag){
                        out.set_error(408, "command timed out");
                    }else if(retcode != 0){
                        out.set_error(500, "command failed");
                    }

                    std::string mode = get_value(in.payload(), "mode", empty::string);
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
                }catch(const std::exception& e){
                    LOG_ERROR("cmd execution failed: {}", e.what());
                    out.set_error(500, e.what());
                    out["retcode"] = -1;
                    out["stdout"]  = "";
                    out["stderr"]  = e.what();
                }catch(...){
                    LOG_ERROR("cmd execution failed: unknown exception");
                    out.set_error(500, "command execution error: unknown");
                    out["retcode"] = -1;
                    out["stdout"]  = "";
                    out["stderr"]  = "unknown exception";
                }
            }
        };
    }

    int cmd::exec(const std::string& executable,
                  const std::vector<std::string>& args,
                  std::string& pout,
                  std::string& perr,
                  const std::string& stdin_data,
                  int timeout_seconds,
                  bool* timed_out)
    {
        if (timed_out) *timed_out = false;

        boost::asio::io_context ioc;
        namespace bp2 = boost::process::v2;

        // pipes for stdin, stdout and stderr
        boost::asio::writable_pipe stdin_pipe(ioc);
        boost::asio::readable_pipe stdout_pipe(ioc);
        boost::asio::readable_pipe stderr_pipe(ioc);

        // create the process
        boost::system::error_code proc_ec;
        bp2::process proc(
            ioc.get_executor(),
            executable,
            args,
            bp2::process_stdio{
                stdin_pipe,
                stdout_pipe,
                stderr_pipe
            },
            proc_ec
        );

        if(proc_ec) {
            LOG_ERROR("error creating process: {}", proc_ec.message());
            perr = proc_ec.message();
            return EXIT_FAILURE;
        }

        // write stdin data if provided, then close
        if (!stdin_data.empty()) {
            boost::system::error_code write_ec;
            boost::asio::write(stdin_pipe, boost::asio::buffer(stdin_data), write_ec);
            if (write_ec) {
                LOG_WARNING("failed to write stdin: {}", write_ec.message());
            }
        }
        stdin_pipe.close();

        // buffer for stdout
        std::string stdout_data;
        boost::asio::dynamic_string_buffer stdout_buffer(stdout_data, 1024);

        std::function<void(boost::system::error_code, std::size_t)> on_stdout;
        on_stdout = [&](boost::system::error_code ec, std::size_t size) {
            if (!ec) {
                pout.append(stdout_data.data(), size);
                stdout_data.erase(0, size);
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
                LOG_WARNING("{}", std::string_view(stderr_data.data(), size));
                stderr_data.erase(0, size);
                boost::asio::async_read(stderr_pipe, stderr_buffer,
                    boost::asio::transfer_at_least(1), on_stderr);
            }
        };

        // start async read operations
        boost::asio::async_read(stdout_pipe, stdout_buffer,
            boost::asio::transfer_at_least(1), on_stdout);
        boost::asio::async_read(stderr_pipe, stderr_buffer,
            boost::asio::transfer_at_least(1), on_stderr);

        int exit_code = -1;

        // optional timeout
        boost::asio::steady_timer timer(ioc);
        if (timeout_seconds > 0) {
            timer.expires_after(std::chrono::seconds(timeout_seconds));
            timer.async_wait([&](boost::system::error_code ec) {
                if (!ec) {
                    LOG_WARNING("process timed out after {}s, terminating", timeout_seconds);
                    if (timed_out) *timed_out = true;
                    boost::system::error_code kill_ec;
                    proc.terminate(kill_ec);
                }
            });
        }

        // wait until the process finishes
        proc.async_wait([&](boost::system::error_code ec, int native_exit_code) {
            if (!ec) {
                exit_code = bp2::evaluate_exit_code(native_exit_code);
            }
            timer.cancel();
            ioc.stop();
        });

        // run all async operations
        ioc.run();

        // bp2::process behaves like std::thread: its destructor calls
        // std::terminate() if ownership has not been released. async_wait
        // reaps the child but does not flip the internal state to
        // "released", so we must call detach() explicitly. The child is
        // already gone (or was killed by terminate()), so nothing escapes.
        proc.detach();

        return exit_code;
    }

}
