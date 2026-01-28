#include <iostream>
#include <fstream>
#include <string>
#include <spawn.h>
#include "dodc.h"
using namespace std;

// Returns true if a factor was found.
bool do_workunit_cado_nfs (workunit_t & wu) {
    wu.result.args = "cado";
    wu.result.factor = "";
    if (wu.method == "CADO_SNFS") {
        wu.result.method = "SNFS";
        wu.cmdline = "./dodc_cado_nfs.py '" + wu.expr + "' > " + wu.tempfile;
    } else {
        wu.result.method = "GNFS";
        wu.cmdline = "./dodc_cado_nfs.py -g '" + wu.inputnumber + "' > " + wu.tempfile;
    }

    pid_t pid;
    posix_spawnp(
        &pid,
        "/bin/sh",
        nullptr,
        nullptr,
        const_cast<char* const*> (array<const char*, 4>{
            "sh",
            "-c",
            wu.cmdline.c_str(),
            nullptr
        }.data()),
        nullptr);
    waitpid(pid, nullptr, 0);

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
