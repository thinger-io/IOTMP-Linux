#ifndef THINGER_IOTMP_UTIL_PROCESS_CLEANUP_HPP
#define THINGER_IOTMP_UTIL_PROCESS_CLEANUP_HPP

#include <fcntl.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/syscall.h>
#endif
#include <boost/process/v2/error.hpp>

namespace thinger::iotmp {

    // Close every descriptor at or above min_fd that is not already
    // close-on-exec. Meant to run in a forked child between fork and exec:
    // it walks /proc/self/fd with raw syscalls and uses only
    // async-signal-safe calls, so it is safe in that window. Descriptors
    // that are close-on-exec are left for execve to drop (Boost's own
    // error-reporting pipe among them).
    //
    // Boost.Process clears inherited descriptors in the child with
    // close_range(2). That path is compiled in when the build kernel headers
    // are 5.9 or newer, but on a host whose kernel lacks the syscall it
    // returns ENOSYS, Boost does not check the result and does not retry, so
    // the descriptors reach the spawned command and anything it detaches.
    // This does the same cleanup in a form that does not depend on the
    // kernel version. Shared by every place the agent spawns a child
    // (cmd::exec, the streaming command session and the interactive
    // terminal).
    inline void close_inherited_fds_from(int min_fd) noexcept {
#if defined(__linux__)
        struct fd_entry {
            unsigned long long d_ino;
            long long          d_off;
            unsigned short     d_reclen;
            unsigned char      d_type;
            char               d_name[];
        };

        // Best effort: if /proc is not mounted the open fails and nothing is
        // closed. On the affected kernels (< 5.9) /proc is effectively always
        // present, so relying on it here is acceptable.
        int dir_fd = ::open("/proc/self/fd", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (dir_fd < 0) return;

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
                if (fd < min_fd || fd == dir_fd) continue;

                int flags = ::fcntl(fd, F_GETFD);
                if (flags != -1 && !(flags & FD_CLOEXEC))
                    ::close(fd);
            }
        }
        ::close(dir_fd);
#else
        (void)min_fd;
#endif
    }

    // Boost.Process v2 initializer wrapping close_inherited_fds_from. Added
    // after process_stdio so it runs in the child once the standard streams
    // are placed on fd 0/1/2 and before execve, closing every other
    // inherited descriptor.
    struct close_inherited_fds {
        template<typename Launcher, typename Path>
        boost::process::v2::error_code on_exec_setup(
            Launcher& /*launcher*/, const Path& /*executable*/, const char* const* /*cmd_line*/) noexcept
        {
            close_inherited_fds_from(3);
            return boost::process::v2::error_code{};
        }
    };

} // namespace thinger::iotmp

#endif // THINGER_IOTMP_UTIL_PROCESS_CLEANUP_HPP
