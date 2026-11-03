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
#include <fcntl.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/syscall.h>
#endif

namespace thinger::iotmp{

    namespace {
        // Create a pipe whose both ends are close-on-exec. The command keeps
        // its stdio because dup2() onto fd 0/1/2 clears the flag on those
        // targets; every other inherited copy is dropped at exec().
        bool make_cloexec_pipe(int fds[2]) {
#if defined(__linux__)
            return ::pipe2(fds, O_CLOEXEC) == 0;
#else
            if (::pipe(fds) != 0) return false;
            ::fcntl(fds[0], F_SETFD, FD_CLOEXEC);
            ::fcntl(fds[1], F_SETFD, FD_CLOEXEC);
            return true;
#endif
        }

        // Boost.Process initializer that closes the descriptors the child
        // inherits beyond its standard streams, so a command that hands work
        // to a detached worker (e.g. a backup that forks and returns) does not
        // pass a live pipe end on to it and leave cmd::exec waiting for an EOF
        // that never comes. Boost performs this cleanup with close_range(2),
        // which requires Linux 5.9; where the syscall is absent it returns
        // ENOSYS and the descriptors stay open, so the cleanup is repeated here
        // in a form that does not depend on the kernel version. on_exec_setup
        // runs in the forked child, after process_stdio has placed the pipe
        // ends on fd 0/1/2 and before execve. It touches only descriptors >= 3
        // that are not already close-on-exec, walks /proc/self/fd with raw
        // syscalls, and uses nothing that is unsafe between fork and exec, so
        // close-on-exec descriptors (Boost's own error-reporting pipe among
        // them) are left for execve to drop.
        struct close_inherited_fds {
            template<typename Launcher, typename Path>
            boost::process::v2::error_code on_exec_setup(
                Launcher& /*launcher*/, const Path& /*executable*/, const char* const* /*cmd_line*/) noexcept
            {
#if defined(__linux__)
                struct fd_entry {
                    unsigned long long d_ino;
                    long long          d_off;
                    unsigned short     d_reclen;
                    unsigned char      d_type;
                    char               d_name[];
                };

                int dir_fd = ::open("/proc/self/fd", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
                if (dir_fd >= 0) {
                    alignas(long long) char buf[4096];
                    for (;;) {
                        long n = ::syscall(SYS_getdents64, dir_fd, buf, sizeof(buf));
                        if (n <= 0) break;
                        for (long off = 0; off < n;) {
                            auto* e = reinterpret_cast<fd_entry*>(buf + off);
                            if (e->d_reclen == 0) break;
                            off += e->d_reclen;

                            int fd = 0;
                            const char* p = e->d_name;
                            if (*p < '0' || *p > '9') continue;
                            for (; *p >= '0' && *p <= '9'; ++p) fd = fd * 10 + (*p - '0');
                            if (*p != '\0') continue;
                            if (fd < 3 || fd == dir_fd) continue;

                            int flags = ::fcntl(fd, F_GETFD);
                            if (flags != -1 && !(flags & FD_CLOEXEC))
                                ::close(fd);
                        }
                    }
                    ::close(dir_fd);
                }
#endif
                return boost::process::v2::error_code{};
            }
        };
    }

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

        // Pipes for stdin, stdout and stderr, created close-on-exec so the
        // only copies the command and its descendants inherit are the ones
        // dup2() installs on fd 0/1/2. A descendant the command detaches
        // (e.g. `setsid ... </dev/null >>log 2>&1 & disown`) then holds no
        // write end, so stdout/stderr reach EOF when the command itself exits
        // rather than when the whole descendant tree does.
        int in_fds[2], out_fds[2], err_fds[2];
        if(!make_cloexec_pipe(in_fds) || !make_cloexec_pipe(out_fds) || !make_cloexec_pipe(err_fds)){
            LOG_ERROR("error creating pipes");
            perr = "failed to create pipes";
            return EXIT_FAILURE;
        }

        boost::asio::writable_pipe stdin_pipe(ioc);
        boost::asio::readable_pipe stdout_pipe(ioc);
        boost::asio::readable_pipe stderr_pipe(ioc);
        stdin_pipe.assign(in_fds[1]);
        stdout_pipe.assign(out_fds[0]);
        stderr_pipe.assign(err_fds[0]);

        // create the process with the child-side end of each pipe. The
        // close_inherited_fds initializer runs after process_stdio in the
        // child and drops every other inherited descriptor before execve.
        boost::system::error_code proc_ec;
        bp2::process proc(
            ioc.get_executor(),
            executable,
            args,
            bp2::process_stdio{ in_fds[0], out_fds[1], err_fds[1] },
            close_inherited_fds{},
            proc_ec
        );

        // The parent keeps only the ends wrapped above; its copies of the
        // child-side ends must go or stdout/stderr would never reach EOF.
        ::close(in_fds[0]);
        ::close(out_fds[1]);
        ::close(err_fds[1]);

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

        // buffers for stdout and stderr
        std::string stdout_data;
        boost::asio::dynamic_string_buffer stdout_buffer(stdout_data, 1024);
        std::string stderr_data;
        boost::asio::dynamic_string_buffer stderr_buffer(stderr_data, 1024);

        int exit_code = -1;

        // timeout timer, declared before the handlers that cancel it
        boost::asio::steady_timer timer(ioc);

        // A process can exit with output still buffered in the pipe, so the
        // loop runs until stdout and stderr have both been drained to EOF and
        // the process has been reaped. Each of those three completions
        // decrements the counter; the last one cancels the timer so ioc.run()
        // has no work left and returns.
        int pending = 3;
        auto on_pending_done = [&]() {
            if (--pending == 0) timer.cancel();
        };

        std::function<void(boost::system::error_code, std::size_t)> on_stdout;
        on_stdout = [&](boost::system::error_code ec, std::size_t size) {
            if (!ec) {
                pout.append(stdout_data.data(), size);
                stdout_data.erase(0, size);
                boost::asio::async_read(stdout_pipe, stdout_buffer,
                    boost::asio::transfer_at_least(1), on_stdout);
            } else {
                on_pending_done();
            }
        };

        std::function<void(boost::system::error_code, std::size_t)> on_stderr;
        on_stderr = [&](boost::system::error_code ec, std::size_t size) {
            if (!ec) {
                perr.append(stderr_data.data(), size);
                LOG_WARNING("{}", std::string_view(stderr_data.data(), size));
                stderr_data.erase(0, size);
                boost::asio::async_read(stderr_pipe, stderr_buffer,
                    boost::asio::transfer_at_least(1), on_stderr);
            } else {
                on_pending_done();
            }
        };

        // start async read operations
        boost::asio::async_read(stdout_pipe, stdout_buffer,
            boost::asio::transfer_at_least(1), on_stdout);
        boost::asio::async_read(stderr_pipe, stderr_buffer,
            boost::asio::transfer_at_least(1), on_stderr);

        // Optional timeout. On expiry, terminate the process and close the
        // pipes so the reads reach their end even if a descendant inherited
        // them and keeps them open.
        if (timeout_seconds > 0) {
            timer.expires_after(std::chrono::seconds(timeout_seconds));
            timer.async_wait([&](boost::system::error_code ec) {
                if (!ec) {
                    LOG_WARNING("process timed out after {}s, terminating", timeout_seconds);
                    if (timed_out) *timed_out = true;
                    boost::system::error_code kill_ec;
                    proc.terminate(kill_ec);
                    boost::system::error_code close_ec;
                    stdout_pipe.close(close_ec);
                    stderr_pipe.close(close_ec);
                }
            });
        }

        // reap the process and record its exit code; the reads keep draining
        proc.async_wait([&](boost::system::error_code ec, int native_exit_code) {
            if (!ec) {
                exit_code = bp2::evaluate_exit_code(native_exit_code);
            }
            on_pending_done();
        });

        // run until the pipes are drained to EOF and the process is reaped
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
