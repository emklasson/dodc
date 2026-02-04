#if !defined(__dodc_h_included)
#define __dodc_h_included

#include "multiprocessing.h"
#include <map>
#include <string>
using namespace std;

const string version = "v1.40";

typedef unsigned int uint;
typedef unsigned int uint32;
typedef unsigned long long uint64;

struct factor_t {
    string factorline, method, args;
    factor_t(string _factorline, string _method, string _args) {
        factorline = _factorline;
        method = _method;
        args = _args;
    }
};

struct workunit_result {
    string factor, method, args;
    // workunit_result( string _factor, string _method, string _args ) {
    //	factor = _factor; method = _method; args = _args;
    // }
};

struct workunit_t {
    string inputnumber;
    string tempfile;
    string cmdline;
    string method;
    string b1;
    bool schedule_bg; // Schedule work in Background process? (macOS)
    workunit_result result;
    bool (*handler)(workunit_t &wu);

    bool enhanced;
    string expr;

    int threadnumber;
};

extern map<string, string> cfg;  // configuration data from .ini file and cmdline
extern map<string, bool> okargs; // allowed configuration arguments. <name,required>

string toupper(string in);
string stripws(string in);
string tostring(long long n);
int toint(string s);
uint64 touint64(string s);
bool isnumber(string s);
char tohex(int n);
string urlencode(string in);
string scientify(string n);

#endif
