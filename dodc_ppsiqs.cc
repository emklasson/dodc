// isn't multithread safe

// #include <iostream>
// #include <fstream>
// #include <sstream>
// #include <algorithm>
// #include <string>
// #include <cctype>
// #include <ctime>
// #include <vector>
// #include <map>
// #include <set>
// #include "dodc.h"
// using namespace std;
//
////returns true if a factor was found
// bool do_workunit_ppsiqs( string inputnumber, workunit_result & result ) {
//	string	line;
//	bool foundfactor = false;
//	//system( ( "echo " + inputnumber + " | " + cfg["ecmcmd"] + " -c " + cfg["curves"] + " " + cfg["ecmargs"] + " " + cfg["b1"] + " > " + cfg["ecmresultfile"] ).c_str() );
//	system( ( "echo " + inputnumber + " | " + "ppsiqs" ).c_str() );
//	ifstream ftmp( "siqs.log" );
//	while( getline( ftmp, line ) ) {
//		uint32 pos = (uint32)line.find( inputnumber );
//		if( pos != line.npos ) {
//			foundfactor = true;
//			result.method = "PPSIQS";
//			result.args = "";
//			getline( ftmp, line );
//			stringstream ss( line );
//			string fsize;
//			char c;
//			ss >> fsize >> c >> result.factor;
//			break;
//		}
//	}
//	ftmp.close();
//	return foundfactor;
// }
