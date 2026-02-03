
#include <algorithm>
#include <direct.h>
#include <iostream>
#pragma warning(push)
#pragma warning(disable : 4146)
#pragma warning(pop)
#include "dodc.h"
#include <fstream>
#include <map>
#include <sstream>
#include <string>
using namespace std;

// returns true if a factor was found
bool do_workunit_ggnfs_snfs(workunit_t &wu) {
    wu.result.method = "SNFS";
    wu.result.args = "";
    wu.result.factor = "";

    ofstream fo(wu.tempfile.c_str());
    fo << wu.threadnumber << endl
       << wu.inputnumber << endl
       << wu.expr << endl
       << cfg["compositeurl"] << endl
       << cfg["wgetcmd"] << endl;
    fo.close();
    system(("donfs " + wu.tempfile).c_str());

    ifstream fi(wu.tempfile.c_str());
    getline(fi, wu.result.factor); // can contain more than one factor, space-separated

    return wu.result.factor != "";
}
