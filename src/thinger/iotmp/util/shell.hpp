#ifndef THINGER_IOTMP_UTIL_SHELL_HPP
#define THINGER_IOTMP_UTIL_SHELL_HPP

#include <cstdlib>
#include <filesystem>
#include <string>

#ifndef _WIN32
#include <pwd.h>
#include <unistd.h>
#endif

namespace thinger::iotmp {

    // Returns the absolute path of the shell the agent should use for both
    // non-interactive command execution (cmd extension) and interactive
    // terminal sessions. Preference order: zsh, bash, sh, ash. Resolved once
    // per process and cached.
    inline const std::string& preferred_shell() {
        static const std::string shell = [] {
            for (const char* name : {"zsh", "bash", "sh", "ash"}) {
                std::string path = std::string("/bin/") + name;
                if (std::filesystem::exists(path)) return path;
            }
            return std::string("/bin/sh");
        }();
        return shell;
    }

    // Returns the name of the preferred shell without its directory (e.g.,
    // "bash"). Useful for interactive terminal sessions that execve with a
    // plain basename.
    inline const std::string& preferred_shell_name() {
        static const std::string name = [] {
            const std::string& full = preferred_shell();
            auto slash = full.find_last_of('/');
            return slash == std::string::npos ? full : full.substr(slash + 1);
        }();
        return name;
    }

    // Populates $HOME in the current process environment if it is unset.
    // Child processes launched by the agent inherit this, so scripts that
    // rely on HOME (cd ~, tilde expansion) keep working even when the
    // service was started without User= or on older systemd that does not
    // populate HOME. Idempotent and safe to call multiple times.
    inline void ensure_home_env() {
#ifndef _WIN32
        static const bool done = [] {
            if (std::getenv("HOME")) return true;
            if (passwd* pw = getpwuid(getuid()); pw && pw->pw_dir && *pw->pw_dir) {
                setenv("HOME", pw->pw_dir, 0);
            }
            return true;
        }();
        (void)done;
#endif
    }

} // namespace thinger::iotmp

#endif // THINGER_IOTMP_UTIL_SHELL_HPP
