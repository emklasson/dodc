#if !defined(__multiprocessing_h_included)
#define __multiprocessing_h_included

#include <string>
#include <spawn.h>
using namespace std;

pair<int, pid_t> spawn(string cmdline);
pair<bool, int> spawn_and_wait(string cmdline);

#endif
