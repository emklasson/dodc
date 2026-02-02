#include "multiprocessing.h"
using namespace std;

int main(int argc, char ** argv) {
	if (argc < 2) {
		return 1;
	}

	setpriority( PRIO_DARWIN_PROCESS, 0, PRIO_DARWIN_BG ); // Only schedules on efficiency cores.
	auto [success, exit_code] = spawn_and_wait(argv[1]);
	return exit_code;
}
