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
#include "dodc.h"
using namespace std;

//returns true if a factor was found
bool do_workunit_msieve( workunit_t & wu ) {
	workunit_result & result = wu.result;
	string	line;
	bool foundfactor = false;
	//if( nfs ) args = "-n " + args;
	while( !foundfactor ) {
		//system( ( "msieve -l " + wu.tempfile + /*" " + args*/ + " " + wu.inputnumber ).c_str() );
		system( ( "msieve -l " + wu.tempfile + ".log -s " + wu.tempfile + ".dat" /*+ " " + args*/ + " " + wu.inputnumber ).c_str() );
		//ifstream ftmp( "msieve.log" );
		ifstream ftmp( ( wu.tempfile + ".log" ).c_str() );
		while( getline( ftmp, line ) ) {
			uint32 pos = (uint32)line.find( wu.inputnumber );
			if( pos != line.npos ) {
				//if( nfs ) {
				//	result.method = "MSIEVENFS";
				//} else {
					result.method = "MSIEVEQS";
				//}
				result.args = "";

				while( getline( ftmp, line ) ) {
					pos = (uint32)line.find( "factor:" );
					if( pos != line.npos ) {
						stringstream ss( line.substr( pos + 7 ) );
						ss >> ws >> result.factor;
						//cout << "find: " << result.factor << endl;
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
