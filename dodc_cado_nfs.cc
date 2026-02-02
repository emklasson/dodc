#include <iostream>
#include <fstream>
#include <string>
#include "dodc.h"
using namespace std;

// Returns true if a factor was found.
bool do_workunit_cado_nfs (workunit_t & wu) {
    wu.result.args = "cado";
    wu.result.factor = "";
    if (wu.method == "CADO_SNFS") {
        wu.result.method = "SNFS";
        wu.cmdline = "./dodc_cado_nfs.py '" + wu.expr + "' -c " + wu.inputnumber + " > " + wu.tempfile;
    } else {
        wu.result.method = "GNFS";
        wu.cmdline = "./dodc_cado_nfs.py -g '" + wu.inputnumber + "' > " + wu.tempfile;
    }

	if (wu.schedule_bg) {
		// setpriority( PRIO_DARWIN_PROCESS, 0, PRIO_DARWIN_BG ); // Only schedules on efficiency cores.
		wu.cmdline = "./schedule_bg '" + wu.cmdline + "'";
	}

	if (!spawn_and_wait(wu.cmdline).first) {
        cout << "ERROR: Failed spawning cado-nfs.\n";
		return false;
	}

    string tag = "factors: ";
	string line;
    ifstream ftmp(wu.tempfile);
    while (getline(ftmp, line)) {
        // if (line.find("time") != line.npos) {
        //     cout << line << endl;
        // }
        if (line.find(tag) != line.npos) {
            wu.result.factor = line.substr(tag.size());
            break;
        } else {
            cout << line << endl;
        }
    }

    ftmp.close();
	return wu.result.factor != "";
}
