#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <string>
#include <cctype>
#include <ctime>
#include <vector>
#include <map>
#include <set>
#include <spawn.h>
#include "dodc.h"
using namespace std;

//returns true if a factor was found
bool do_workunit_gmp_ecm( workunit_t & wu ) {
	workunit_result & result = wu.result;
	string	line;
	bool foundfactor = false;
	// system( ( "echo " + inputnumber + " | " + cfg["ecmcmd"] + " -c " + cfg["curves"] + " " + cfg["ecmargs"] + " " + cfg["b1"] + " > " + cfg["ecmresultfile"] ).c_str() );
	// system( wu.cmdline.c_str() );

	if (wu.schedule_bg) {
		// setpriority( PRIO_DARWIN_PROCESS, 0, PRIO_DARWIN_BG ); // Only schedules on efficiency cores.
		wu.cmdline = "./schedule_bg '" + wu.cmdline + "'";
	}

	if (!spawn_and_wait(wu.cmdline).first) {
        cout << "ERROR: Failed spawning ecm.\n";
		return false;
	}

	//ifstream ftmp( cfg["ecmresultfile"].c_str() );
	ifstream ftmp( wu.tempfile.c_str() );
	//uint64 sigma = 0;
    string sigma = "";
	while( getline( ftmp, line ) ) {
		auto pos = line.find( "sigma=" );
		if( pos != line.npos ) {
            pos += 6;
            auto pos2 = line.find_first_of( ", \r\n", pos );
            if( pos2 == line.npos ) {
                sigma = line.substr( pos );
            } else {
                sigma = line.substr( pos, pos2 - pos );
            }
			//sigma = touint64( line.substr( pos + 6 ) );
		}
		pos = line.find( ": " );
		if( pos != line.npos ) {
			foundfactor = true;
			string p = line.substr( pos + 2 );
			//ofstream flog( cfg["sigmafile"].c_str(), ios::app );
			//flog << p << " sigma=" << sigma << endl;
			//flog.close();

			result.method = wu.method;	//TODO: replace this with something more concrete... we might have been called using an automethod
			result.factor = p;
			result.args = "B1=" + scientify( wu.b1 );
			if( toupper( wu.method ) == "ECM" ) {
				result.args += ",s=" + sigma;
			}
			//found_factor( p, enhanced, expr, inputnumber, sigma );
			//bool trialfactor = true;
			//if( trialfactor && p.size() <= 10 ) {
			//	uint64	n = touint64( p );
			//	for( uint64 f = 3; f * f <= n; f += 2 ) {
			//		if( !( n % f ) ) {
			//			found_factor( tostring( f ), enhanced, expr, inputnumber, sigma );
			//			do {
			//				n /= f;
			//			} while( !( n % f ) );
			//		}
			//	}
			//	if( n > 1 && n != touint64( p ) ) {
			//		found_factor( tostring( n ), enhanced, expr, inputnumber, sigma );
			//	}
			//}
			break;
		}
	}
	ftmp.close();
	return foundfactor;
}
