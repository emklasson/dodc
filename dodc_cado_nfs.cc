#include "dodc.h"
#include <fstream>
#include <string>
using namespace std;

/// @brief Runs CADO-NFS on a workunit.
/// @param wu The workunit to run CADO-NFS on.
/// @return True if a factor was found, otherwise false.
bool do_workunit_cado_nfs(workunit_t &wu) {
    wu.result.args = "cado";
    wu.result.factor = "";
    if (wu.method == "CADO_SNFS") {
        wu.result.method = "SNFS";
        wu.cmdline = "./dodc_cado_nfs.py '" + wu.expr + "' -c " + wu.inputnumber + " > " + wu.tempfile;
    } else {
        wu.result.method = "GNFS";
        wu.cmdline = "./dodc_cado_nfs.py -g '" + wu.inputnumber + "' > " + wu.tempfile;
    }

    if (!spawn_and_wait(wu.cmdline_prefix + wu.cmdline).first) {
        log("ERROR: Failed spawning cado-nfs.\n");
        return false;
    }

    string tag = "factors: ";
    string line;
    ifstream ftmp(wu.tempfile);
    while (getline(ftmp, line)) {
        if (line.find(tag) != line.npos) {
            wu.result.factor = line.substr(tag.size());
            break;
        } else {
            log("{}\n", line);
        }
    }

    return wu.result.factor != "";
}
