#if !defined(__multiprocessing_h_included)
#define __multiprocessing_h_included

#include <spawn.h>
#include <string>
using namespace std;

pair<int, pid_t> spawn(string cmdline);
pair<bool, int> spawn_and_wait(string cmdline);

#endif
