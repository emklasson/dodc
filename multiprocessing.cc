#include "multiprocessing.h"
#include <array>
#include <utility>
using namespace std;

extern char **environ; // For posix_spawnp.

/// @brief Spawns a child process.
/// @param cmdline Cmdline to spawn.
/// @return std::pair(posix_spawnp_return_code, child_pid)
pair<int, pid_t> spawn(string cmdline) {
    pid_t pid;
    int r = posix_spawnp(
        &pid,
        "/bin/sh",
        nullptr,
        nullptr,
        const_cast<char *const *>(array<const char *, 4>{
            "sh",
            "-c",
            cmdline.c_str(),
            nullptr}.data()),
        environ);

    return {r, pid};
}

/// @brief Spawns a child process and waits for it to finish.
///
/// child_process_exit_code is only valid if spawn_success is true.
/// @param cmdline Cmdline to spawn.
/// @return std::pair(spawn_success, child_process_exit_code)
pair<bool, int> spawn_and_wait(string cmdline) {
    auto [spawn_return_code, pid] = spawn(cmdline);
    bool success = false;
    int exit_code = -1;
    if (spawn_return_code == 0) {
        int status = 0;
        waitpid(pid, &status, 0);
        if (WIFEXITED(status)) {
            success = true;
            exit_code = WEXITSTATUS(status);
        }
    }

    return {success, exit_code};
}
