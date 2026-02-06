#if !defined(__dodc_h_included)
#define __dodc_h_included

#include "multiprocessing.h"
#include <cstdint>
#include <string>
using namespace std;

const string version = "v1.40";

struct factor_t {
    string factorline, method, args;
    factor_t(string _factorline, string _method, string _args) {
        factorline = _factorline;
        method = _method;
        args = _args;
    }
};

struct workunit_result_t {
    string factor, method, args;
};

struct workunit_t {
    string inputnumber;
    string tempfile;
    string cmdline;
    string method;
    string b1;
    bool schedule_bg; // Schedule work in Background process? (macOS)
    workunit_result_t result;
    bool (*handler)(workunit_t &wu);

    bool enhanced;
    string expr;

    int threadnumber;
};

#endif
