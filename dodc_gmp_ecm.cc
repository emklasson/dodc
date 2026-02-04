#include "dodc.h"
#include <algorithm>
#include <cctype>
#include <ctime>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <spawn.h>
#include <sstream>
#include <string>
#include <vector>
using namespace std;

/// @brief Runs GMP-ECM on a workunit.
/// @param wu The workunit to run GMP-ECM on.
/// @return True if a factor was found, otherwise false.
bool do_workunit_gmp_ecm(workunit_t &wu) {
    bool foundfactor = false;

    if (wu.schedule_bg) {
        // setpriority( PRIO_DARWIN_PROCESS, 0, PRIO_DARWIN_BG ); // Only schedules on efficiency cores.
        wu.cmdline = "./schedule_bg '" + wu.cmdline + "'";
    }

    if (!spawn_and_wait(wu.cmdline).first) {
        cout << "ERROR: Failed spawning ecm.\n";
        return false;
    }

    ifstream ftmp(wu.tempfile);
    string sigma = "";
    string line;
    while (getline(ftmp, line)) {
        auto pos = line.find("sigma=");
        if (pos != line.npos) {
            pos += 6;
            auto pos2 = line.find_first_of(", \r\n", pos);
            if (pos2 == line.npos) {
                sigma = line.substr(pos);
            } else {
                sigma = line.substr(pos, pos2 - pos);
            }
        }

        pos = line.find(": ");
        if (pos != line.npos) {
            foundfactor = true;
            string p = line.substr(pos + 2);

            wu.result.method = wu.method;
            wu.result.factor = p;
            wu.result.args = "B1=" + scientify(wu.b1);
            if (toupper(wu.method) == "ECM") {
                wu.result.args += ",s=" + sigma;
            }
            break;
        }
    }

    return foundfactor;
}
