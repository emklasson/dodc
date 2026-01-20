
#include <iostream>
#include <direct.h>
#include <algorithm>
#pragma warning(push)
#pragma warning(disable: 4146)
#pragma warning(pop)
#include <sstream>
#include <fstream>
#include <string>
#include <map>
#include "dodc.h"
using namespace std;


//returns true if a factor was found
bool do_workunit_yafu( workunit_t & wu ) {
	wu.result.method = "QS";
	wu.result.args = "";
	wu.result.factor = "";

	ofstream fo( wu.tempfile.c_str() );
	fo 	<< wu.threadnumber << endl
		<< wu.inputnumber << endl
		<< wu.expr << endl
		<< cfg["compositeurl"] << endl
		<< cfg["wgetcmd"] << endl;
	fo.close();
	system( ( "doyafu " + wu.tempfile ).c_str() );

	ifstream fi( wu.tempfile.c_str() );
	getline( fi, wu.result.factor );	//can contain more than one factor, space-separated
	fi.close();

	return wu.result.factor != "";
}
