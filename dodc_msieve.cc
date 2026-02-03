#include "dodc.h"
#include <algorithm>
#include <cctype>
#include <ctime>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>
using namespace std;

// returns true if a factor was found
bool do_workunit_msieve(workunit_t &wu) {
    workunit_result &result = wu.result;
    string line;
    bool foundfactor = false;
    // if( nfs ) args = "-n " + args;
    while (!foundfactor) {
        // system( ( "msieve -l " + wu.tempfile + /*" " + args*/ + " " + wu.inputnumber ).c_str() );
        //  system( ( "msieve -t 1 -p -l " + wu.tempfile + ".log -s " + wu.tempfile + ".dat" /*+ " " + args*/ + " " + wu.inputnumber ).c_str() );
        wu.cmdline = "msieve -t 1 -p -l " + wu.tempfile + ".log -s " + wu.tempfile + ".dat" /*+ " " + args*/ + " " + wu.inputnumber;

        if (wu.schedule_bg) {
            // setpriority( PRIO_DARWIN_PROCESS, 0, PRIO_DARWIN_BG ); // Only schedules on efficiency cores.
            wu.cmdline = "./schedule_bg '" + wu.cmdline + "'";
        }

        if (!spawn_and_wait(wu.cmdline).first) {
            cout << "ERROR: Failed spawning msieve.\n";
            return false;
        }

        // ifstream ftmp( "msieve.log" );
        ifstream ftmp((wu.tempfile + ".log").c_str());
        while (getline(ftmp, line)) {
            auto pos = line.find(wu.inputnumber);
            if (pos != line.npos) {
                // if( nfs ) {
                //	result.method = "MSIEVENFS";
                // } else {
                result.method = "MSIEVEQS";
                //}
                result.args = "";

                while (getline(ftmp, line)) {
                    pos = line.find("factor:");
                    if (pos != line.npos) {
                        stringstream ss(line.substr(pos + 7));
                        ss >> ws >> result.factor;
                        // cout << "find: " << result.factor << endl;
                        foundfactor = true;
                        break;
                    }
                }
                break;
            }
        }
        ftmp.close();
    }
    return foundfactor;
}
