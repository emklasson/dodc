/*
dodc (C) Mikael Klasson
fluff@mklasson.com
http://mklasson.com

TODO: specifying "1e8" etc as B1 only works because gmp-ecm parses it. dodc doesn't understand it and interprets it as "1" when applying b1increase.
*/

#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <string>
#include <cctype>
#include <ctime>
#include <random>
#include <vector>
#include <map>
#include <set>
#include <queue>
#include "dodc.h"
#include "dodc_gmp_ecm.h"
#include <thread>
#include <semaphore>
#include <format>
//#include "dodc_ppsiqs.h"
#include "dodc_msieve.h"
#include "dodc_cado_nfs.h"
// #include "dodc_ggnfs.h"
// #include "dodc_yafu.h"
using namespace std;

// #include <windows.h>
// #include <process.h>

counting_semaphore hsem_wu{0};
mutex hmutex_wu;
mutex hmutex_wu_result;

queue<int> thread_numbers;
queue<workunit_t> wu_result_queue;

map<string,string>	cfg;	//configuration data from .ini file and cmdline
map<string,bool>	okargs;	//allowed configuration arguments. <name,required>

string	okmethods[] = { "ECM", "P-1", "P+1", "MSIEVEQS", "CADO_SNFS", "CADO_GNFS" };//, "GGNFS_SNFS", "YAFU_QS" };	//supported methods

string toupper( string in ) {
	string s = in;
	for( uint32 j = 0; j < s.size(); ++j ) {
		s[j] = toupper( s[j] );
	}

	return s;
}

string stripws( string in ) {
	string	s;
	for( uint32 j = 0; j < in.size(); ++j ) {
		if( !isspace( in[j] ) ) {
			s = in.substr( j );
			break;
		}
	}

	while( s.size() && isspace( s[s.size() - 1] ) ) {
		s.resize( s.size() - 1 );
	}

	return s;
}

string tostring( long long n ) {
	stringstream ss;
	ss << n;
	return ss.str();
}

int toint( string s ) {
	stringstream ss( s );
	int n;
	ss >> n;
	return n;
}

uint64 touint64( string s ) {
	stringstream ss( s );
	uint64 n;
	ss >> n;
	return n;
}

bool isnumber( string s ) {
	for( uint32 j = 0; j < s.size(); ++j ) {
		if( !isdigit( s[j] ) ) {
			return false;
		}
	}

	return true;
}

char tohex( int n ) {
	return string("0123456789abcdef")[n % 16];
}

string urlencode( string in ) {
	string	s;
	for( uint32 j = 0; j < in.size(); ++j ) {
		if( isalnum( in[j] ) ) {
			s += in[j];
		} else if( in[j] == ' ' ) {
			s += '+';
		} else {
			s += '%';
			s += tohex( in[j] / 16 );
			s += tohex( in[j] % 16 );
		}
	}

	return s;
}

// "11000000" -> "11e6"
string scientify( string n ) {
	int	e = 0;
	while( n.size() > 1 && n[n.size() - 1] == '0' ) {
		++e;
		n.resize( n.size() - 1 );
	}

	if( e < 4 ) {
		return n + string( e, '0' );
	}

	return n + "e" + tostring( e );
}

bool dump_factor( factor f ) {
	ofstream	fout( cfg["submitfailurefile"].c_str(), ios::app );
	fout << "#method(" << f.method << ")" << "args(" << f.args << ")" << endl;
	fout << f.factorline << endl;
	fout.close();
	return true;
}

// Submit multiple factors, one per line, with a comment line above each in the
// format: "#method(methodname)args(args)".
// Failures must be handled by caller as factors will not be saved in here.
// Sets factors[].second to false for each submitted factor.
// Returns number of submitted factors.
int submit_factors( vector<pair<factor,bool>> &factors ) {
	int successes = 0;
	string factorlines;
	for (auto i = 0; i < factors.size(); ++i) {
		factorlines += (i ? "\n" : "")
			+ format("#method({})args({})\n", factors[i].first.method, factors[i].first.args)
			+ factors[i].first.factorline;
	}
	string	postdata = "name=" + urlencode( cfg["name"] )
		+ "&method=unknown"
		+ "&factors=" + urlencode( factorlines )
		+ "&args=unknown"
		+ "&submitter=" + urlencode( "dodc " + version )
		;
	remove( cfg["wgetresultfile"].c_str() );

	cout << "Sending factors to server..." << flush;
	int r = system( ( cfg["wgetcmd"] + " -q --cache=off --output-document=\"" + cfg["wgetresultfile"] + "\" --post-data=\"" + postdata + "\" " + cfg["submiturl"] ).c_str() );
	cout << " Done." << endl;
	if( r != 0 ) {
		cout << "WARNING: wget returned " << r << ". There was probably an error." << endl;
	}

	ifstream f( cfg["wgetresultfile"].c_str() );
	string	line;
	string prefix = "JSONResults:";
	while (getline(f, line)) {
		if (line.substr(0, prefix.size()) == prefix) {
			// cout << line << endl;
			for (auto f : factors) {
				f.second = line.find(f.first.factorline) == line.npos;
				successes += f.second ? 0 : 1;
			}
		}
	}

	f.close();
	if( !successes ) {
		cout << "ERROR! Couldn't parse submission result." << endl;
	}

	return successes;
}

//  Returns true if submitinterval has passed since last time that happened.
bool submit_interval_passed() {
	static time_t lastattempt = 0;
	auto now = time(0);
	if (now - toint( cfg["submitinterval"] ) * 60 >= lastattempt) {
		lastattempt = now;
		return true;
	}

	return false;
}

bool submit_factor( string factorline, string method, string args, bool retryattempt, bool forceattempt ) {
	bool failure = false;
	bool submit = true;
	if( !submit_interval_passed() && !forceattempt ) {
		submit = false;
	}

	if (submit) {
		string	postdata = "name=" + urlencode( cfg["name"] )
			+ "&method=" + urlencode( method )
			+ "&factors=" + urlencode( factorline )
			+ "&args=" + urlencode( args )
			+ "&submitter=" + urlencode( "dodc " + version )
			;
		remove( cfg["wgetresultfile"].c_str() );

		cout << "Sending factor to server..." << flush;
		int r = system( ( cfg["wgetcmd"] + " -q --cache=off --output-document=\"" + cfg["wgetresultfile"] + "\" --post-data=\"" + postdata + "\" " + cfg["submiturl"] ).c_str() );
		cout << " Done." << endl;
		if( r != 0 ) {
			cout << "WARNING: wget returned " << r << ". There was probably an error." << endl;
			failure = true;
		}

		ifstream f( cfg["wgetresultfile"].c_str() );
		string	line;
		bool	found = false;
		while( getline( f, line ) ) {
			size_t pos = line.find( factorline );
			if( pos != line.npos ) {
				if( ( pos = line.find( "</td><td>", pos ) ) != line.npos ) {
					pos += string( "</td><td>" ).size();
					size_t last = line.find( "</td>", pos + 1 );
					if( last != line.npos ) {
						found = true;
						cout << "Result: " << line.substr( pos, last - pos ) << endl;
						break;
					}
				}
			}
		}

		f.close();
		if( !found ) {
			cout << "ERROR! Couldn't parse submission result." << endl;
			failure = true;
		}
	}

	if( !submit || failure ) {
		if( !retryattempt ) {
			dump_factor( factor( factorline, method, args ) );
			cout << "Your factor has been saved in " << cfg["submitfailurefile"] << " for now." << endl;
		}

		return false;
	}

	return true;
}

bool report_work() {
	string	url = cfg["reporturl"]
		+ "?method=" + urlencode( cfg["method"] )
		+ "&numbers=" + urlencode( cfg["numbers"] )
		+ "&nmin=" + urlencode( cfg["nmin"] )
		+ "&nmax=" + urlencode( cfg["nmax"] )
		+ "&b1=" + urlencode( cfg["b1"] )
		+ "&curves=" + urlencode( cfg["curves"] )
		;
	cout << "Reporting completed work..." << flush;
	int r = system( ( cfg["wgetcmd"]
			+ " -q --cache=off --output-document=\""
			+ cfg["wgetresultfile"]
			+ "\" \"" + url + "\"" ).c_str() );
	cout << " Done. Thanks!" << endl;

	if( r != 0 ) {
		cout << "WARNING: wget returned " << r << ". There was probably an error." << endl;
	}

	return r == 0;
}

bool download_composites() {
	string	url = cfg["compositeurl"]
		+ "?enhanced=true&sort=" + urlencode( cfg["sort"] )
		+ "&nmin=" + urlencode( cfg["nmin"] )
		+ "&nmax=" + urlencode( cfg["nmax"] )
		+ "&order=" + urlencode( cfg["order"] )
		+ "&gzip=" + urlencode( cfg["use_gzip"] )
		+ "&recommendedwork=" + urlencode( cfg["recommendedwork"] )
		;
	stringstream	ss( cfg["numbers"] );
	int	k;
	char	plusminus,comma;
	int	filenr = 1;
	while( ss >> k >> plusminus ) {
		url += "&file" + tostring( filenr++ ) + "=" + tostring( k ) + "t2rn" + ( plusminus == '+' ? "p" : "m" ) + "1.txt";
		ss >> comma;
	}

	cout << "Downloading composites..." << flush;
	int r = system( ( cfg["wgetcmd"]
			+ " -q --cache=off --output-document=\""
			+ cfg["compositefile"]
			+ ( cfg["use_gzip"] == "yes" ? ".gz" : "" )
			+ "\" \"" + url + "\"" ).c_str() );
	cout << " Done." << endl;

	bool probablyerror = false;
	if( r != 0 ) {
		cout << "WARNING: wget returned " << r << ". There was probably an error." << endl;
		probablyerror = true;
	}

	if( cfg["use_gzip"] == "yes" ) {
		r = system( ( cfg["gzipcmd"] + " -df " + cfg["compositefile"] + ".gz" ).c_str() );
		if( r != 0 ) {
			cout << "WARNING: gzip returned " << r << ". There was probably an error unpacking composites." << endl;
			probablyerror = true;
		}
	}

	return probablyerror;
}

// Returns true if all previously unsubmitted factors were submitted ok now.
void process_unsubmitted_factors( bool forceattempt ) {
	static time_t	lastattempt = 0;
	if( !forceattempt && ( time( 0 ) - toint( cfg["submitretryinterval"] ) * 60 < lastattempt ) ) {
		return;
	}

	lastattempt = time( 0 );
	ifstream	fin( cfg["submitfailurefile"].c_str() );
	if( !fin.is_open() ) {
		//assume file doesn't exist
		return;
	}

	map<string,string>	info;
	info["method"] = cfg["method"];	//use this if method isn't stored in file
	string	line;
	vector<pair<factor,bool> >	unsubmitted;
	while( getline( fin, line ) ) {
		if( !line.size() ) {
			continue;
		}

		//is there extra info present?
		if( line[0] == '#' ) {
			stringstream	ss( line.substr( 1 ) );
			string	fn,val;
			while( getline( ss, fn, '(' ) ) {
				getline( ss, val, ')' );
				info[fn] = val;
			}
			continue;
		}
		unsubmitted.push_back( make_pair( factor( line, info["method"], info["args"] ), true ) );
	}
	fin.close();

	//no unsubmitted factors?
	if( !unsubmitted.size() ) {
		return;
	}

	cout << "Trying to send your unsubmitted factors..." << endl;
	int succeeded = submit_factors(unsubmitted);

	// int	succeeded = 0;
	// for( vector<pair<factor,bool> >::iterator i = unsubmitted.begin(); i != unsubmitted.end(); ++i ) {
	// 	if( submit_factor( i->first.factorline, i->first.method, i->first.args, true, true ) ) {
	// 		++succeeded;
	// 		i->second = false;
	// 	}
	// }

	string	failstring = "Please submit the factors in " + cfg["submitfailurefile"] + " manually\n"
			+ "or wait and see if dodc manages to submit them automatically later.\n"
			+ "Manual submit: " + cfg["manualsubmiturl"] + "\n";

	if( !succeeded ) {
		//none succeeded, just exit.
		cout << "Didn't manage to submit any factor now either." << endl;
		cout << failstring;
		return;
	}

	//remove all old failures
	remove( cfg["submitfailurefile"].c_str() );
	if( succeeded == unsubmitted.size() ) {
		cout << "Submitted all " << tostring(succeeded) << " of your unsubmitted factors!" << endl;
		return;
	} else {
		cout << "Submitted " << tostring( succeeded ) << " of your "
			<< (uint) unsubmitted.size() << " unsubmitted factors!" << endl;
		cout << failstring;
	}

	//dump remaining unsubmitted to file
	for( vector<pair<factor,bool> >::iterator i = unsubmitted.begin(); i != unsubmitted.end(); ++i ) {
		if( i->second ) {
			dump_factor( i->first );
		}
	}

	return;
}

bool cfg_set( string src, string arg, string val ) {
	if( !okargs.count( arg ) ) {
		cout << "WARNING: unrecognized option: '" << arg << "'" << endl;
		return false;
	}

	cout << src << ": " << arg << " = " << val << endl;
	cfg[arg] = val;
	return true;
}

bool parse_cmdline( int argc, char ** argv ) {
	for( int j = 1; j < argc; ) {
		if( argv[j][0] == '-' ) {
			string	arg = string( argv[j] ).substr( 1 );
			if( arg == "h" || arg == "-help" || arg == "?" ) {
				return false;	// just display help info, then exit
			}

			if( j + 1 >= argc ) {
				cout << "ERROR: cmdline switch without argument: " << argv[j] << endl;
				return false;
  			}

			cfg_set( "cmdline", arg, argv[j + 1] );
			j += 2;
		} else {
			cout << "WARNING: unrecognized cmdline argument: " << argv[j] << endl;
			++j;
		}
	}

	return true;
}

bool read_inifile( string fname ) {
	ifstream f( fname.c_str() );
	string	line,arg,val;

	if( !f.is_open() ) {
		cout << "ERROR: couldn't open .ini file: " << fname << endl;
		cout << "Look in the dodc archive for an example of a proper .ini file." << endl;
		return false;
	}

	while( getline( f, line ) ) {
		stringstream	ss( line );
		getline( ss, arg, '=' );
		ss >> ws;
		arg = stripws( arg );
		if( !getline( ss, val ) ) {
			continue;
		}

		val = stripws( val );
		if( arg.substr( 0, 2 ) == "//" || arg.substr( 0, 1 ) == "#" || arg.substr( 0, 1 ) == ";" ) {
			continue;
		}

		cfg_set( "ini", arg, val );
	}

	f.close();
	return true;
}

bool init_args() {
	string reqargs[] = {	"name", "b1", "b1increase", "curves", "nmin", "nmax", "loop", "numbers",
				"autosubmit", "autodownload", "reportwork", "wgetcmd", "ecmcmd", "gzipcmd",
				"use_gzip", "sort", "order", "compositefile", "compositeurl", "submiturl",
				"manualsubmiturl", "reporturl", "factorfile", "submitfailurefile", "sigmafile",
				"wgetresultfile", "ecmresultfile", "recommendedwork", "method", "submitretryinterval",
				"worker_threads", "submitinterval"
	};
	string optargs[] = { "ecmargs", "fallback", "automethod", "less_spam" };
	for( int j = 0; j < sizeof( reqargs ) / sizeof( string ); ++j ) {
		okargs[reqargs[j]] = true;
	}

	for( int j = 0; j < sizeof( optargs ) / sizeof( string ); ++j ) {
		okargs[optargs[j]] = false;
	}

	return true;
}

//verifies that a method is known and supported
bool verify_method( string method ) {
	for( uint j = 0; j < sizeof( okmethods ) / sizeof( string ); ++j ) {
		if( method == okmethods[j] ) {
			return true;
		}
	}

	return false;
}

bool verify_args() {
	bool	ok = true;
	for( map<string,bool>::iterator i = okargs.begin(); i != okargs.end(); ++i ) {
		if( i->second && cfg[i->first] == "" ) {
			cout << "ERROR: missing required setting: " << i->first << endl;
			ok = false;
		}
	}

	if( toint( cfg["nmin"] ) > toint( cfg["nmax"] ) ) {
		cout << "ERROR: nmin=" << cfg["nmin"] << " > nmax=" << cfg["nmax"] << " doesn't make sense." << endl;
		ok = false;
	}

	cfg["method"] = toupper( cfg["method"] );
	if( !verify_method( cfg["method"] ) ) {
		cout << "ERROR: unrecognized method: " << cfg["method"] << endl;
		ok = false;
	}

	if( cfg["automethod"] != "" ) {
		stringstream ss( cfg["automethod"] );
		string amethod;
		while( getline( ss, amethod, ',' ) ) {
			uint32 minsize,maxsize;
			char c;
			ss >> minsize >> c >> maxsize >> c;	//skip fields and ';'
			if( !verify_method( amethod ) ) {
				cout << "ERROR: unrecognized automethod: " << amethod << endl;
				ok = false;
			}
		}
	}

	if( cfg["method"] == "P-1" ) {
		cfg["ecmargs"] += " -pm1";
		if( toint( cfg["curves"] ) != 1 ) {
			cout << "WARNING: running " << cfg["curves"] << " P-1 tests doesn't make sense. Forcing <curves> to 1." << endl;
			cfg["curves"] = 1;
		}
	} else if( cfg["method"] == "P+1" ) {
		cfg["ecmargs"] += " -pp1";
		if( toint( cfg["curves"] ) > 3 ) {
			cout << "WARNING: running " << cfg["curves"] << " P+1 tests is probably a waste. Try 3 instead." << endl;
		}
	}

	return ok;
}

void found_factor( string foundfactor, bool enhanced, string expr, string inputnumber, string method, string args ) {
	ofstream	fout( cfg["factorfile"].c_str(), ios::app );
	stringstream	factorline;
	if( enhanced ) {
		factorline << foundfactor << " | " << expr;
	} else {
		factorline << foundfactor << " | " << inputnumber;
	}

	cout << factorline.str() << "\t" << "(" << tostring( foundfactor.size() ) << " digits)" << endl;
	fout << factorline.str() << endl;
	if( cfg["autosubmit"] == "yes" ) {
		dump_factor(factor(factorline.str(), method, args));
	}

	fout.close();
}

// returns the number of composites in <compositefile>
// also shuffles them if "order = random"
// processes recommended work settings from server if "recommendedwork = yes"
int init_composites() {
	int cnt = 0;
	string	line;
	if( cfg["recommendedwork"] == "yes" ) {
		ifstream f( cfg["compositefile"].c_str() );
		getline( f, line );
		if( line.substr( 0, 1 ) != "#" ) {
			cout << "WARNING: trying to do recommended work, but didn't get any settings from server." << endl;
		} else {
			stringstream	ss( line.substr( 1 ) );
			ss >> ws;
			string	fn,val;
			while( getline( ss, fn, '(' ) ) {
				getline( ss, val, ')' );
				cfg_set( "recommended work", fn, val );
			}
		}
		f.close();
	}

	ifstream f( cfg["compositefile"].c_str() );
	if( cfg["order"] == "random" ) {
		vector<pair<string,string> >	v;
		string	comment;
		while( getline( f, line ) ) {
			if( line.substr( 0, 1 ) != "#" ) {
				++cnt;
				v.push_back( make_pair( line, comment ) );
				comment = "";
			} else {
				comment = line;
			}
		}

		f.close();
		random_device rd;
		mt19937 g(rd());
		shuffle( v.begin(), v.end(), g );
		ofstream	fout( cfg["compositefile"].c_str() );
		for( uint32 j = 0; j < v.size(); ++j ) {
			if( v[j].second != "" ) {
				fout << v[j].second << endl;
			}

			fout << v[j].first << endl;
		}

		fout.close();
	} else {
		while( getline( f, line ) ) {
			if( line.substr( 0, 1 ) != "#" ) {
				++cnt;
			}
		}

		f.close();
	}

	return cnt;
}

void free_workunit( workunit_t * pwu ) {
	lock_guard<mutex>	lock( hmutex_wu );

	thread_numbers.push( pwu->threadnumber );
	delete pwu;
	hsem_wu.release();
}

workunit_t * get_workunit() {
	hsem_wu.acquire();
	lock_guard<mutex>	lock( hmutex_wu );

	if( !thread_numbers.size() ) {
		cout << "ERROR: couldn't get a new thread number!";
		exit( 0 );
	}

	workunit_t * pwu = new workunit_t;
	pwu->threadnumber = thread_numbers.front();
	thread_numbers.pop();

	return pwu;
}

void add_wu_result( workunit_t * pwu ) {
	lock_guard<mutex>	lock( hmutex_wu_result );

	wu_result_queue.push( *pwu );
}

// Returns true if a found factor was handled.
bool process_wu_results() {
	lock_guard<mutex>	lock( hmutex_wu_result );

	bool found = false;
	if( wu_result_queue.size() ) {
		workunit_t & wu = wu_result_queue.front();
		workunit_result & result = wu.result;
		stringstream ss( wu.result.factor );
		string factor;
		while( ss >> factor ) {
			found_factor( factor, wu.enhanced, wu.expr, wu.inputnumber, wu.result.method, wu.result.args );

			//trial factor found factor if it's small
			if( factor.size() <= 10 ) {
				uint64	n = touint64( factor );
				for( uint64 f = 3; f * f <= n; f += 2 ) {
					if( !( n % f ) ) {
						found_factor( tostring( f ), wu.enhanced, wu.expr, wu.inputnumber, wu.result.method, wu.result.args );
						do {
							n /= f;
						} while( !( n % f ) );
					}
				}

				if( n > 1 && n != touint64( factor ) ) {
					found_factor( tostring( n ), wu.enhanced, wu.expr, wu.inputnumber, wu.result.method, wu.result.args );
				}
			}
		}

		if (submit_interval_passed()) {
			process_unsubmitted_factors(true);
		} else {
			cout << "Factor(s) saved in " << cfg["submitfailurefile"] << " for now." << endl;
		}

		wu_result_queue.pop();
		found = true;
	}

	return found;
}

void process_workunit( void * p ) {
	workunit_t * pwu = (workunit_t *) p;

	if( pwu->handler( *pwu ) ) add_wu_result( pwu );

	free_workunit( pwu );
}

// Returns true if a factor was found.
void do_workunit( string inputnumber, bool enhanced, string expr ) {
	workunit_t * pwu = get_workunit();
	workunit_t & wu = *pwu;

	wu.inputnumber = inputnumber;
	wu.enhanced = enhanced;
	wu.expr = expr;

	string method = cfg["method"];
	if( cfg["automethod"] != "" ) {
		stringstream ss( cfg["automethod"] );
		string amethod;
		while( getline( ss, amethod, ',' ) ) {
			uint32 minsize,maxsize;
			char c;
			ss >> minsize >> c >> maxsize;
			if( minsize <= inputnumber.size() && maxsize >= inputnumber.size() ) {
				method = amethod;
				break;
			}

			ss >> c;	//skip ";"
		}
	}

	cout << "[" << wu.threadnumber << "] ";
	method = toupper( method );
	string msg;
	if( enhanced ) {
		msg = format("Factoring {} c{}", expr, inputnumber.size());
	} else {
		msg = format("Factoring {}", inputnumber);
	}

	string tab = string(8 - (msg.size() % 8), ' ');
	msg = format("{}{}[{}]    ", msg, tab, method);
	if (cfg["less_spam"] == "yes") {
		cout << msg << "\r" << flush;
	} else {
		cout << msg << endl;
	}

	//TODO: check if handlers exist for methods specified in automethods when dodc starts

	if( method == "MSIEVEQS" ) {
		wu.tempfile = "msieve" + tostring( wu.threadnumber );
		wu.method = method;
		wu.handler = do_workunit_msieve;
		//foundfactor = do_workunit_msieve( wu );
	// //} else if( method == "MSIEVENFS" ) {
	// //	wu.tempfile = "msieve.log" + tostring( threadnum );
	// //	wu.method = method;
	// //	foundfactor = do_workunit_msieve( wu, result, true );
	// } else if( method == "GGNFS_SNFS" ) {
	// 	wu.tempfile = "dodc_ggnfs_snfs_" + tostring( wu.threadnumber );
	// 	wu.method = method;
	// 	wu.handler = do_workunit_ggnfs_snfs;
	// } else if( method == "YAFU_QS" ) {
	// 	wu.tempfile = "dodc_yafu_qs_" + tostring( wu.threadnumber );
	// 	wu.method = method;
	// 	wu.handler = do_workunit_yafu;
	} else if( method == "CADO_SNFS" || method == "CADO_GNFS" ) {
		wu.tempfile = "dodc_cado_nfs_" + tostring( wu.threadnumber );
		wu.method = method;
		wu.handler = do_workunit_cado_nfs;
	} else {
		wu.tempfile = cfg["ecmresultfile"] + tostring( wu.threadnumber );
		wu.cmdline = "echo " + wu.inputnumber + " | " + cfg["ecmcmd"] + " -c " + cfg["curves"] + " " + cfg["ecmargs"] + " " + cfg["b1"] + " > " + wu.tempfile;
		wu.method = cfg["method"];
		wu.b1 = cfg["b1"];
		wu.handler = do_workunit_gmp_ecm;
		//foundfactor = do_workunit_gmp_ecm( wu );
	}

	// _beginthread( process_workunit, 0, pwu );
	thread t( process_workunit, pwu );
	t.detach();
}

bool init_synchronization() {
	int threads = toint( cfg["worker_threads"] );
	hsem_wu.release( threads );

	for( int j = 1; j <= threads; ++j ) {
		thread_numbers.push( j );
	}

	return true;
}

//sets process priority from 0 to 4 with 0 being idle and 4 being high.
void set_priority( int priority = 0 ) {
#if defined(WIN32)
	unsigned int aprio[] = { IDLE_PRIORITY_CLASS, BELOW_NORMAL_PRIORITY_CLASS, NORMAL_PRIORITY_CLASS,
		ABOVE_NORMAL_PRIORITY_CLASS, HIGH_PRIORITY_CLASS };
	unsigned int atprio[] = { THREAD_PRIORITY_IDLE, THREAD_PRIORITY_LOWEST, THREAD_PRIORITY_BELOW_NORMAL,
		THREAD_PRIORITY_NORMAL, THREAD_PRIORITY_ABOVE_NORMAL, THREAD_PRIORITY_HIGHEST };
	SetPriorityClass( GetCurrentProcess(), aprio[priority] );
	SetThreadPriority( GetCurrentThread(), atprio[0] );
#else	//linux
	setpriority( PRIO_PROCESS, 0, 20 - 10 * priority );
#endif
}

int main( int argc, char ** argv ) {
	cout << "dodc " << version << " by Mikael Klasson (mklasson@gmail.com)" << endl;
	cout << "usage: dodc [<settings>]" << endl;
	cout << "  Make sure you're using the right username." << endl;
	cout << "  Look in dodc.ini for available options." << endl;
	srand( (uint) time( 0 ) );
	init_args();
	read_inifile( "dodc.ini" );
	if( !parse_cmdline( argc, argv ) ) {
		return 0;
	}

	if( !verify_args() ) {
		cout << "Fix your settings and try again. Exiting." << endl;
		return 1;
	}

	cout << "Using factorization method " << cfg["method"] << endl;

	set_priority();

	if( !init_synchronization() ) return 1;

	int	totalfactors = 0;
	do {
		process_unsubmitted_factors( true );

		if( cfg["autodownload"] == "yes" ) {
			download_composites();
		}

		int ccnt = init_composites();
		cout << "Found " << ccnt << " composites in " << cfg["compositefile"] << "." << endl;
		if( !ccnt ) {
			//compositefile is empty
			if( cfg["fallback"] == "yes" && cfg["recommendedwork"] != "yes" ) {
				cout << "Switching to fallback mode." << endl;
				cfg["recommendedwork"] = "yes";
				cfg["autodownload"] = "yes";
				continue;
			} else {
				cout << "Work complete. Exiting." << endl;
				break;
			}
		}

		ifstream fin( cfg["compositefile"].c_str() );
		bool	enhanced = false;
		string	line,expr;
		while( getline( fin, line ) ) {
			if( line.substr( 0, 1 ) == "#" ) {
				stringstream	ss( line.substr( 1 ) );
				ss >> ws;
				string	fn,val;
				while( getline( ss, fn, '(' ) ) {
					getline( ss, val, ')' );
					if( fn == "expr" ) {
						expr = val;
						enhanced = true;
					}
				}

				continue;
			}

			do_workunit( line, enhanced, expr );
			while( process_wu_results() ) {
				cout << "#factors found: " << ++totalfactors << "    " << endl;
			}

			process_unsubmitted_factors( false );
		}

		fin.close();
		cout << "#factors found: " << totalfactors << "    " << endl;
		if( cfg["reportwork"] == "yes" ) {
			report_work();
		}

		//increase b1
		cfg["b1"] = tostring( touint64( cfg["b1"] ) + touint64( cfg["b1increase"] ) );
		cout << "Increasing B1 to " << cfg["b1"] << endl;
	} while( cfg["loop"] == "yes" );

	cout << "Waiting for worker threads to finish...";
	while( thread_numbers.size() != toint( cfg["worker_threads"] ) ) {
		this_thread::sleep_for( chrono::milliseconds( 1000 ) );
		while( process_wu_results() ) {
			cout << endl << "#factors found: " << ++totalfactors << endl;
		}

		process_unsubmitted_factors( false );
	}

	cout << endl;
	while( process_wu_results() ) {
		cout << "#factors found: " << ++totalfactors << endl;
	}

	//do one last valiant attempt to submit any unsubmitted factors
	process_unsubmitted_factors( true );

	remove( cfg["wgetresultfile"].c_str() );
	remove( cfg["ecmresultfile"].c_str() );

	cout << "All done! Exiting." << endl;
}
