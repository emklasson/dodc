#include "dodc.h"
#include <fstream>
#include <sstream>
#include <string>
using namespace std;

/// @brief Runs msieve on a workunit.
/// @param wu The workunit to run msieve on.
/// @return True if a factor was found, otherwise false.
bool do_workunit_msieve(workunit_t &wu) {
    string line;
    bool foundfactor = false;
    while (!foundfactor) {
        wu.cmdline = "msieve -t 1 -p -l " + wu.tempfile + ".log -s " + wu.tempfile + ".dat" + " " + wu.inputnumber;

        if (wu.schedule_bg) {
            // setpriority( PRIO_DARWIN_PROCESS, 0, PRIO_DARWIN_BG ); // Only schedules on efficiency cores.
            wu.cmdline = "./schedule_bg '" + wu.cmdline + "'";
        }

        if (!spawn_and_wait(wu.cmdline).first) {
            log("ERROR: Failed spawning msieve.\n");
            return false;
        }

        ifstream ftmp(wu.tempfile + ".log");
        while (getline(ftmp, line)) {
            auto pos = line.find(wu.inputnumber);
            if (pos != line.npos) {
                wu.result.method = "MSIEVE_QS";
                wu.result.args = "";

                while (getline(ftmp, line)) {
                    pos = line.find("factor:");
                    if (pos != line.npos) {
                        stringstream ss(line.substr(pos + 7));
                        ss >> ws >> wu.result.factor;
                        foundfactor = true;
                        break;
                    }
                }
                break;
            }
        }
    }

    return foundfactor;
}
